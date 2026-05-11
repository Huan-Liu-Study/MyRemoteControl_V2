#include "handlers/ScreenHandler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"

namespace {

constexpr uint32_t DEFAULT_STREAM_INTERVAL_MS = 200;
constexpr uint32_t SCREEN_STREAM_FRAME_KEY = 1;
constexpr uint32_t SCREEN_STREAM_FRAME_DELTA = 2;
constexpr uint64_t KEY_FRAME_INTERVAL = 30;
constexpr uint32_t DELTA_BLOCK_SIZE = 64;
constexpr size_t MAX_DELTA_RECTS = 128;

struct CapturedScreenFrame {
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    uint32_t captureWidth = 0;
    uint32_t captureHeight = 0;
    ByteBuffer pixels;
};

struct ChangedRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct FrameTiming {
    uint32_t captureMs = 0;
    uint32_t compareMs = 0;
    uint32_t encodeMs = 0;
    uint32_t sendMs = 0;
};

enum class StreamControlEvent {
    None,
    Stop,
    KeyFrameRequest
};

bool getEncoderClsid(const WCHAR* mimeType, CLSID& outClsid)
{
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok || encoderBytes == 0) {
        return false;
    }

    std::vector<uint8_t> buffer(encoderBytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok) {
        return false;
    }

    for (UINT i = 0; i < encoderCount; ++i) {
        if (wcscmp(encoders[i].MimeType, mimeType) == 0) {
            outClsid = encoders[i].Clsid;
            return true;
        }
    }

    return false;
}

bool readStreamBytes(IStream* stream, ByteBuffer& outBytes)
{
    STATSTG stat{};
    if (stream->Stat(&stat, STATFLAG_NONAME) != S_OK || stat.cbSize.QuadPart == 0) {
        return false;
    }

    if (stat.cbSize.QuadPart > static_cast<ULONGLONG>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }

    LARGE_INTEGER begin{};
    if (stream->Seek(begin, STREAM_SEEK_SET, nullptr) != S_OK) {
        return false;
    }

    ByteBuffer bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG bytesRead = 0;
    if (stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &bytesRead) != S_OK) {
        return false;
    }

    if (bytesRead != bytes.size()) {
        return false;
    }

    outBytes = std::move(bytes);
    return true;
}

ULONG clampJpegQuality(uint32_t quality)
{
    if (quality < 10) {
        return 10;
    }

    if (quality > 95) {
        return 95;
    }

    return static_cast<ULONG>(quality);
}

uint32_t clampScalePercent(uint32_t scalePercent)
{
    if (scalePercent < 25) {
        return 25;
    }

    if (scalePercent > 100) {
        return 100;
    }

    return scalePercent;
}

uint32_t clampStreamIntervalMs(uint32_t intervalMs)
{
    if (intervalMs < 100) {
        return 100;
    }

    if (intervalMs > 5000) {
        return 5000;
    }

    return intervalMs;
}

bool encodeBitmapToJpeg(HBITMAP bitmap, uint32_t requestedQuality, ByteBuffer& outImage, std::string& errorMessage)
{
    Gdiplus::GdiplusStartupInput startupInput{};
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) {
        errorMessage = "GDI+ startup failed.";
        return false;
    }

    bool ok = false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
        CLSID jpegClsid{};
        Gdiplus::Bitmap image(bitmap, nullptr);

        Gdiplus::EncoderParameters encoderParams{};
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
        encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG quality = clampJpegQuality(requestedQuality);
        encoderParams.Parameter[0].Value = &quality;

        if (getEncoderClsid(L"image/jpeg", jpegClsid)
            && image.Save(stream, &jpegClsid, &encoderParams) == Gdiplus::Ok
            && readStreamBytes(stream, outImage)) {
            ok = true;
        }

        stream->Release();
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (!ok) {
        errorMessage = "JPEG encode failed.";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool encodePixelsToJpeg(
    const ByteBuffer& pixels,
    uint32_t width,
    uint32_t height,
    uint32_t requestedQuality,
    ByteBuffer& outImage,
    std::string& errorMessage
)
{
    if (pixels.empty() || width == 0 || height == 0) {
        outImage.clear();
        errorMessage.clear();
        return true;
    }

    Gdiplus::GdiplusStartupInput startupInput{};
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) {
        errorMessage = "GDI+ startup failed.";
        return false;
    }

    bool ok = false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
        CLSID jpegClsid{};
        const INT stride = static_cast<INT>(width * 4);
        Gdiplus::Bitmap image(
            static_cast<INT>(width),
            static_cast<INT>(height),
            stride,
            PixelFormat32bppRGB,
            const_cast<BYTE*>(pixels.data())
        );

        Gdiplus::EncoderParameters encoderParams{};
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
        encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG quality = clampJpegQuality(requestedQuality);
        encoderParams.Parameter[0].Value = &quality;

        if (getEncoderClsid(L"image/jpeg", jpegClsid)
            && image.Save(stream, &jpegClsid, &encoderParams) == Gdiplus::Ok
            && readStreamBytes(stream, outImage)) {
            ok = true;
        }

        stream->Release();
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);

    if (!ok) {
        errorMessage = "JPEG encode failed.";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool capturePrimaryScreenPixels(
    uint32_t quality,
    uint32_t scalePercent,
    CapturedScreenFrame& outFrame,
    std::string& errorMessage
)
{
    (void)quality;
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    if (width <= 0 || height <= 0) {
        errorMessage = "Invalid screen size.";
        return false;
    }

    const uint32_t scale = clampScalePercent(scalePercent);
    const int captureWidth = (std::max)(1, width * static_cast<int>(scale) / 100);
    const int captureHeight = (std::max)(1, height * static_cast<int>(scale) / 100);

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        errorMessage = "GetDC failed.";
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!memoryDc) {
        ReleaseDC(nullptr, screenDc);
        errorMessage = "CreateCompatibleDC failed.";
        return false;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, captureWidth, captureHeight);
    if (!bitmap) {
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        errorMessage = "CreateCompatibleBitmap failed.";
        return false;
    }

    HGDIOBJ oldObject = SelectObject(memoryDc, bitmap);
    SetStretchBltMode(memoryDc, HALFTONE);
    const BOOL copied = StretchBlt(memoryDc, 0, 0, captureWidth, captureHeight, screenDc, 0, 0, width, height, SRCCOPY);
    SelectObject(memoryDc, oldObject);

    if (!copied) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        errorMessage = "Screen capture failed.";
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = captureWidth;
    bitmapInfo.bmiHeader.biHeight = -captureHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    ByteBuffer pixels(static_cast<size_t>(captureWidth) * static_cast<size_t>(captureHeight) * 4);
    const int lines = GetDIBits(
        screenDc,
        bitmap,
        0,
        static_cast<UINT>(captureHeight),
        pixels.data(),
        &bitmapInfo,
        DIB_RGB_COLORS
    );

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (lines != captureHeight) {
        errorMessage = "GetDIBits failed.";
        return false;
    }

    outFrame.screenWidth = static_cast<uint32_t>(width);
    outFrame.screenHeight = static_cast<uint32_t>(height);
    outFrame.captureWidth = static_cast<uint32_t>(captureWidth);
    outFrame.captureHeight = static_cast<uint32_t>(captureHeight);
    outFrame.pixels = std::move(pixels);
    errorMessage.clear();
    return true;
}

bool capturePrimaryScreenJpeg(
    uint32_t quality,
    uint32_t scalePercent,
    uint32_t& outScreenWidth,
    uint32_t& outScreenHeight,
    ByteBuffer& outImage,
    std::string& errorMessage
)
{
    CapturedScreenFrame frame{};
    if (!capturePrimaryScreenPixels(quality, scalePercent, frame, errorMessage)) {
        return false;
    }

    outScreenWidth = frame.screenWidth;
    outScreenHeight = frame.screenHeight;
    return encodePixelsToJpeg(frame.pixels, frame.captureWidth, frame.captureHeight, quality, outImage, errorMessage);
}

ChangedRect findChangedRect(const CapturedScreenFrame& previous, const CapturedScreenFrame& current)
{
    if (previous.captureWidth != current.captureWidth
        || previous.captureHeight != current.captureHeight
        || previous.pixels.size() != current.pixels.size()) {
        return {0, 0, current.captureWidth, current.captureHeight};
    }

    uint32_t minX = current.captureWidth;
    uint32_t minY = current.captureHeight;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
    bool changed = false;

    for (uint32_t y = 0; y < current.captureHeight; ++y) {
        for (uint32_t x = 0; x < current.captureWidth; ++x) {
            const size_t index = (static_cast<size_t>(y) * current.captureWidth + x) * 4;
            if (previous.pixels[index] != current.pixels[index]
                || previous.pixels[index + 1] != current.pixels[index + 1]
                || previous.pixels[index + 2] != current.pixels[index + 2]) {
                changed = true;
                minX = (std::min)(minX, x);
                minY = (std::min)(minY, y);
                maxX = (std::max)(maxX, x);
                maxY = (std::max)(maxY, y);
            }
        }
    }

    if (!changed) {
        return {};
    }

    return {minX, minY, maxX - minX + 1, maxY - minY + 1};
}

std::vector<ChangedRect> findChangedBlocks(const CapturedScreenFrame& previous, const CapturedScreenFrame& current)
{
    if (previous.captureWidth != current.captureWidth
        || previous.captureHeight != current.captureHeight
        || previous.pixels.size() != current.pixels.size()) {
        return {{0, 0, current.captureWidth, current.captureHeight}};
    }

    std::vector<ChangedRect> blocks;
    for (uint32_t y = 0; y < current.captureHeight; y += DELTA_BLOCK_SIZE) {
        for (uint32_t x = 0; x < current.captureWidth; x += DELTA_BLOCK_SIZE) {
            const uint32_t blockWidth = (std::min)(DELTA_BLOCK_SIZE, current.captureWidth - x);
            const uint32_t blockHeight = (std::min)(DELTA_BLOCK_SIZE, current.captureHeight - y);
            bool changed = false;

            for (uint32_t row = 0; row < blockHeight && !changed; ++row) {
                for (uint32_t col = 0; col < blockWidth; ++col) {
                    const size_t index = (static_cast<size_t>(y + row) * current.captureWidth + (x + col)) * 4;
                    if (previous.pixels[index] != current.pixels[index]
                        || previous.pixels[index + 1] != current.pixels[index + 1]
                        || previous.pixels[index + 2] != current.pixels[index + 2]) {
                        changed = true;
                        break;
                    }
                }
            }

            if (changed) {
                blocks.push_back({x, y, blockWidth, blockHeight});
            }
        }
    }

    return blocks;
}

ChangedRect boundingRect(const std::vector<ChangedRect>& rects)
{
    if (rects.empty()) {
        return {};
    }

    uint32_t minX = rects.front().x;
    uint32_t minY = rects.front().y;
    uint32_t maxX = rects.front().x + rects.front().width;
    uint32_t maxY = rects.front().y + rects.front().height;

    for (const ChangedRect& rect : rects) {
        minX = (std::min)(minX, rect.x);
        minY = (std::min)(minY, rect.y);
        maxX = (std::max)(maxX, rect.x + rect.width);
        maxY = (std::max)(maxY, rect.y + rect.height);
    }

    return {minX, minY, maxX - minX, maxY - minY};
}

ByteBuffer copyRectPixels(const CapturedScreenFrame& frame, const ChangedRect& rect)
{
    if (rect.width == 0 || rect.height == 0) {
        return {};
    }

    ByteBuffer rectPixels(static_cast<size_t>(rect.width) * static_cast<size_t>(rect.height) * 4);
    for (uint32_t y = 0; y < rect.height; ++y) {
        const size_t sourceOffset = (static_cast<size_t>(rect.y + y) * frame.captureWidth + rect.x) * 4;
        const size_t targetOffset = static_cast<size_t>(y) * rect.width * 4;
        std::copy(
            frame.pixels.begin() + sourceOffset,
            frame.pixels.begin() + sourceOffset + static_cast<size_t>(rect.width) * 4,
            rectPixels.begin() + targetOffset
        );
    }

    return rectPixels;
}

uint32_t elapsedMsSince(std::chrono::steady_clock::time_point startedAt)
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt
        ).count()
    );
}

ScreenshotStartRequest parseScreenshotRequest(const ByteBuffer& payload)
{
    ScreenshotStartRequest request{};
    request.quality = 70;
    request.scalePercent = 100;

    if (payload.empty()) {
        return request;
    }

    if (!deserializeScreenshotStartRequest(payload, request)) {
        request.quality = 70;
        request.scalePercent = 100;
    }

    return request;
}

bool sendScreenshotStartError(SOCKET clientSock, const std::string& message)
{
    ScreenshotStartResponse response{};
    response.ok = 0;
    response.imageSize = 0;
    response.errorMessage = message;

    return sendPacket(clientSock, CMD::CMD_SCREENSHOT_START, serializeScreenshotStartResponse(response));
}

bool sendImageChunks(SOCKET clientSock, const ByteBuffer& image, CMD::Type chunkCommand, CMD::Type endCommand)
{
    constexpr size_t chunkSize = 65536;
    size_t offset = 0;
    while (offset < image.size()) {
        const size_t bytesToSend = (std::min)(chunkSize, image.size() - offset);
        ByteBuffer chunk(image.begin() + offset, image.begin() + offset + bytesToSend);

        if (!sendPacket(clientSock, chunkCommand, chunk)) {
            return false;
        }

        offset += bytesToSend;
    }

    return sendPacket(clientSock, endCommand, ByteBuffer{});
}

ScreenStreamStartRequest parseScreenStreamRequest(const ByteBuffer& payload)
{
    ScreenStreamStartRequest request{};
    request.quality = 70;
    request.scalePercent = 100;
    request.intervalMs = DEFAULT_STREAM_INTERVAL_MS;

    if (payload.empty()) {
        return request;
    }

    if (!deserializeScreenStreamStartRequest(payload, request)) {
        request.quality = 70;
        request.scalePercent = 100;
        request.intervalMs = DEFAULT_STREAM_INTERVAL_MS;
    }

    return request;
}

StreamControlEvent waitForStreamControlOrTimeout(SOCKET clientSock, std::chrono::milliseconds timeout)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(clientSock, &readSet);

    timeval waitTime{};
    waitTime.tv_sec = static_cast<long>(timeout.count() / 1000);
    waitTime.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    const int ready = select(0, &readSet, nullptr, nullptr, &waitTime);
    if (ready <= 0) {
        return StreamControlEvent::None;
    }

    ParsedPacket packet{};
    if (!recvPacket(clientSock, packet)) {
        return StreamControlEvent::Stop;
    }

    if (packet.header.command == CMD::CMD_SCREEN_STREAM_STOP) {
        return StreamControlEvent::Stop;
    }

    if (packet.header.command == CMD::CMD_SCREEN_STREAM_KEYFRAME_REQUEST) {
        return StreamControlEvent::KeyFrameRequest;
    }

    return StreamControlEvent::None;
}

} // namespace

bool handleScreenshotStart(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    ByteBuffer image;
    std::string errorMessage;
    const ScreenshotStartRequest request = parseScreenshotRequest(requestPayload);
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    if (!capturePrimaryScreenJpeg(request.quality, request.scalePercent, screenWidth, screenHeight, image, errorMessage)) {
        return sendScreenshotStartError(clientSock, errorMessage);
    }

    ScreenshotStartResponse response{};
    response.ok = 1;
    response.imageSize = static_cast<uint64_t>(image.size());
    response.screenWidth = screenWidth;
    response.screenHeight = screenHeight;
    response.fileName = "screenshot.jpg";
    response.imageFormat = "JPG";

    if (!sendPacket(clientSock, CMD::CMD_SCREENSHOT_START, serializeScreenshotStartResponse(response))) {
        return false;
    }

    return sendImageChunks(clientSock, image, CMD::CMD_SCREENSHOT_CHUNK, CMD::CMD_SCREENSHOT_END);
}

bool handleScreenStreamStart(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    const ScreenStreamStartRequest request = parseScreenStreamRequest(requestPayload);
    const uint32_t intervalMs = clampStreamIntervalMs(request.intervalMs);
    CapturedScreenFrame previousFrame{};
    uint64_t previousFrameId = 0;
    uint64_t nextFrameId = 1;
    uint64_t lastKeyFrameImageSize = 0;
    uint32_t previousSendMs = 0;
    bool forceKeyFrame = false;

    while (true) {
        const auto frameStartedAt = std::chrono::steady_clock::now();
        CapturedScreenFrame currentFrame{};
        ByteBuffer image;
        std::string errorMessage;
        FrameTiming timing{};

        auto stageStartedAt = std::chrono::steady_clock::now();
        if (!capturePrimaryScreenPixels(request.quality, request.scalePercent, currentFrame, errorMessage)) {
            return sendPacket(clientSock, CMD::CMD_ERROR, errorMessage);
        }
        timing.captureMs = elapsedMsSince(stageStartedAt);

        bool sendKeyFrame = forceKeyFrame
            || previousFrame.pixels.empty()
            || ((nextFrameId - 1) % KEY_FRAME_INTERVAL == 0);

        stageStartedAt = std::chrono::steady_clock::now();
        std::vector<ChangedRect> changedRects = sendKeyFrame
            ? std::vector<ChangedRect>{{0, 0, currentFrame.captureWidth, currentFrame.captureHeight}}
            : findChangedBlocks(previousFrame, currentFrame);
        timing.compareMs = elapsedMsSince(stageStartedAt);

        bool fallbackToKeyFrame = false;
        if (!sendKeyFrame && changedRects.size() > MAX_DELTA_RECTS) {
            sendKeyFrame = true;
            fallbackToKeyFrame = true;
            changedRects = {{0, 0, currentFrame.captureWidth, currentFrame.captureHeight}};
        }

        stageStartedAt = std::chrono::steady_clock::now();
        std::vector<ScreenStreamRect> streamRects;
        for (const ChangedRect& changedRect : changedRects) {
            if (changedRect.width == 0 || changedRect.height == 0) {
                continue;
            }

            const ByteBuffer rectPixels = copyRectPixels(currentFrame, changedRect);
            ByteBuffer rectImage;

            if (!encodePixelsToJpeg(
                    rectPixels,
                    changedRect.width,
                    changedRect.height,
                    request.quality,
                    rectImage,
                    errorMessage
                )) {
                return sendPacket(clientSock, CMD::CMD_ERROR, errorMessage);
            }

            streamRects.push_back({
                changedRect.x,
                changedRect.y,
                changedRect.width,
                changedRect.height,
                static_cast<uint64_t>(rectImage.size())
            });
            image.insert(image.end(), rectImage.begin(), rectImage.end());
        }
        timing.encodeMs = elapsedMsSince(stageStartedAt);

        if (sendKeyFrame) {
            lastKeyFrameImageSize = static_cast<uint64_t>(image.size());
        }

        const ChangedRect summaryRect = boundingRect(changedRects);

        ScreenStreamFrameHeader frameHeader{};
        frameHeader.imageSize = static_cast<uint64_t>(image.size());
        frameHeader.screenWidth = currentFrame.screenWidth;
        frameHeader.screenHeight = currentFrame.screenHeight;
        frameHeader.captureWidth = currentFrame.captureWidth;
        frameHeader.captureHeight = currentFrame.captureHeight;
        frameHeader.frameType = sendKeyFrame ? SCREEN_STREAM_FRAME_KEY : SCREEN_STREAM_FRAME_DELTA;
        frameHeader.frameId = nextFrameId;
        frameHeader.baseFrameId = sendKeyFrame ? 0 : previousFrameId;
        frameHeader.rectX = summaryRect.x;
        frameHeader.rectY = summaryRect.y;
        frameHeader.rectWidth = summaryRect.width;
        frameHeader.rectHeight = summaryRect.height;
        frameHeader.rectCount = static_cast<uint32_t>(streamRects.size());
        frameHeader.estimatedFullImageSize = sendKeyFrame ? static_cast<uint64_t>(image.size()) : lastKeyFrameImageSize;
        frameHeader.captureMs = timing.captureMs;
        frameHeader.compareMs = timing.compareMs;
        frameHeader.encodeMs = timing.encodeMs;
        frameHeader.sendMs = previousSendMs;
        frameHeader.fallbackToKeyFrame = fallbackToKeyFrame ? 1u : 0u;
        frameHeader.rects = std::move(streamRects);
        frameHeader.imageFormat = "JPG";

        stageStartedAt = std::chrono::steady_clock::now();
        if (!sendPacket(clientSock, CMD::CMD_SCREEN_STREAM_FRAME_START, serializeScreenStreamFrameHeader(frameHeader))) {
            return false;
        }

        if (!sendImageChunks(
                clientSock,
                image,
                CMD::CMD_SCREEN_STREAM_FRAME_CHUNK,
                CMD::CMD_SCREEN_STREAM_FRAME_END
            )) {
            return false;
        }
        timing.sendMs = elapsedMsSince(stageStartedAt);
        previousSendMs = timing.sendMs;

        previousFrame = std::move(currentFrame);
        previousFrameId = nextFrameId;
        ++nextFrameId;
        forceKeyFrame = false;

        const auto frameInterval = std::chrono::milliseconds(intervalMs);
        const auto elapsed = std::chrono::steady_clock::now() - frameStartedAt;
        if (elapsed < frameInterval) {
            const StreamControlEvent event = waitForStreamControlOrTimeout(
                    clientSock,
                    std::chrono::duration_cast<std::chrono::milliseconds>(frameInterval - elapsed)
                );
            if (event == StreamControlEvent::Stop) {
                return true;
            }
            if (event == StreamControlEvent::KeyFrameRequest) {
                forceKeyFrame = true;
            }
            continue;
        }

        const StreamControlEvent event = waitForStreamControlOrTimeout(clientSock, std::chrono::milliseconds(0));
        if (event == StreamControlEvent::Stop) {
            return true;
        }
        if (event == StreamControlEvent::KeyFrameRequest) {
            forceKeyFrame = true;
        }
    }
}
