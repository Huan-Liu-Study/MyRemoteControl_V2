#include "handlers/ScreenHandler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>

#include "concurrency/ThreadPool.h"
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
constexpr size_t MAX_DELTA_ENCODE_RECTS = 16;
constexpr uint32_t MAX_BOUNDING_RECT_AREA_PERCENT = 70;
constexpr size_t MIN_BLOCK_ROWS_PER_DIFF_TASK = 4;
constexpr size_t MIN_RECTS_FOR_PARALLEL_ENCODE = 2;
constexpr uint32_t FRAME_ACK_TIMEOUT_MS = 5000;

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
    uint32_t bltMs = 0;
    uint32_t copyMs = 0;
    uint32_t compareMs = 0;
    uint32_t encodeMs = 0;
    uint32_t sendMs = 0;
    uint32_t ackWaitMs = 0;
};

struct EncodedRectResult {
    ChangedRect rect;
    ByteBuffer image;
};

bool getEncoderClsid(const WCHAR* mimeType, CLSID& outClsid);

struct JpegEncoderContext {
    JpegEncoderContext()
    {
        Gdiplus::GdiplusStartupInput startupInput{};
        started = Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok;
        if (started) {
            hasJpegClsid = getEncoderClsid(L"image/jpeg", jpegClsid);
            hasPngClsid = getEncoderClsid(L"image/png", pngClsid);
        }
    }

    ~JpegEncoderContext()
    {
        if (started) {
            Gdiplus::GdiplusShutdown(token);
        }
    }

    bool started = false;
    bool hasJpegClsid = false;
    bool hasPngClsid = false;
    ULONG_PTR token = 0;
    CLSID jpegClsid{};
    CLSID pngClsid{};
};

struct ScreenCaptureContext {
    ~ScreenCaptureContext()
    {
        reset();
    }

    bool ensureSize(int requestedWidth, int requestedHeight, std::string& errorMessage)
    {
        if (requestedWidth <= 0 || requestedHeight <= 0) {
            errorMessage = "Invalid capture size.";
            return false;
        }

        if (screenDc && memoryDc && bitmap && pixels
            && width == requestedWidth
            && height == requestedHeight) {
            return true;
        }

        reset();

        screenDc = GetDC(nullptr);
        if (!screenDc) {
            errorMessage = "GetDC failed.";
            return false;
        }

        memoryDc = CreateCompatibleDC(screenDc);
        if (!memoryDc) {
            errorMessage = "CreateCompatibleDC failed.";
            reset();
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = requestedWidth;
        bitmapInfo.bmiHeader.biHeight = -requestedHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* dibPixels = nullptr;
        bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
        if (!bitmap || !dibPixels) {
            errorMessage = "CreateDIBSection failed.";
            reset();
            return false;
        }

        oldObject = SelectObject(memoryDc, bitmap);
        if (!oldObject || oldObject == HGDI_ERROR) {
            errorMessage = "SelectObject failed.";
            reset();
            return false;
        }

        pixels = static_cast<uint8_t*>(dibPixels);
        width = requestedWidth;
        height = requestedHeight;
        errorMessage.clear();
        return true;
    }

    bool capture(
        int sourceX,
        int sourceY,
        CapturedScreenFrame& outFrame,
        FrameTiming& timing,
        std::string& errorMessage
    )
    {
        if (!screenDc || !memoryDc || !pixels || width <= 0 || height <= 0) {
            errorMessage = "Capture context is not ready.";
            return false;
        }

        const auto bltStartedAt = std::chrono::steady_clock::now();
        const BOOL copied = BitBlt(
            memoryDc,
            0,
            0,
            width,
            height,
            screenDc,
            sourceX,
            sourceY,
            SRCCOPY
        );
        timing.bltMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - bltStartedAt
            ).count()
        );
        if (!copied) {
            errorMessage = "Screen capture failed.";
            return false;
        }

        const auto copyStartedAt = std::chrono::steady_clock::now();
        const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        outFrame.pixels.resize(byteCount);
        std::memcpy(outFrame.pixels.data(), pixels, byteCount);
        timing.copyMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - copyStartedAt
            ).count()
        );
        errorMessage.clear();
        return true;
    }

private:
    void reset()
    {
        if (memoryDc && oldObject && oldObject != HGDI_ERROR) {
            SelectObject(memoryDc, oldObject);
        }

        if (bitmap) {
            DeleteObject(bitmap);
        }

        if (memoryDc) {
            DeleteDC(memoryDc);
        }

        if (screenDc) {
            ReleaseDC(nullptr, screenDc);
        }

        screenDc = nullptr;
        memoryDc = nullptr;
        bitmap = nullptr;
        oldObject = nullptr;
        pixels = nullptr;
        width = 0;
        height = 0;
    }

    HDC screenDc = nullptr;
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldObject = nullptr;
    uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
};

enum class StreamControlEvent {
    None,
    Stop,
    KeyFrameRequest,
    FrameAck
};

struct StreamControlResult {
    StreamControlEvent event = StreamControlEvent::None;
    uint64_t frameId = 0;
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

JpegEncoderContext& jpegEncoderContext()
{
    static JpegEncoderContext context;
    return context;
}

size_t screenWorkerCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads <= 2) {
        return 2;
    }

    return (std::min)(static_cast<size_t>(hardwareThreads - 1), static_cast<size_t>(4));
}

ThreadPool& screenProcessingPool()
{
    static ThreadPool pool(screenWorkerCount());
    return pool;
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

    JpegEncoderContext& context = jpegEncoderContext();
    if (!context.started) {
        errorMessage = "GDI+ startup failed.";
        return false;
    }

    if (!context.hasJpegClsid) {
        errorMessage = "JPEG encoder was not found.";
        return false;
    }

    bool ok = false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
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

        if (image.Save(stream, &context.jpegClsid, &encoderParams) == Gdiplus::Ok
            && readStreamBytes(stream, outImage)) {
            ok = true;
        }

        stream->Release();
    }

    if (!ok) {
        errorMessage = "JPEG encode failed.";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool encodePixelsToPng(
    const ByteBuffer& pixels,
    uint32_t width,
    uint32_t height,
    ByteBuffer& outImage,
    std::string& errorMessage
)
{
    if (pixels.empty() || width == 0 || height == 0) {
        outImage.clear();
        errorMessage.clear();
        return true;
    }

    JpegEncoderContext& context = jpegEncoderContext();
    if (!context.started) {
        errorMessage = "GDI+ startup failed.";
        return false;
    }

    if (!context.hasPngClsid) {
        errorMessage = "PNG encoder was not found.";
        return false;
    }

    bool ok = false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
        const INT stride = static_cast<INT>(width * 4);
        Gdiplus::Bitmap image(
            static_cast<INT>(width),
            static_cast<INT>(height),
            stride,
            PixelFormat32bppRGB,
            const_cast<BYTE*>(pixels.data())
        );

        if (image.Save(stream, &context.pngClsid, nullptr) == Gdiplus::Ok
            && readStreamBytes(stream, outImage)) {
            ok = true;
        }

        stream->Release();
    }

    if (!ok) {
        errorMessage = "PNG encode failed.";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool useLosslessImage(uint32_t quality)
{
    return quality >= 95;
}

bool encodePixelsToScreenImage(
    const ByteBuffer& pixels,
    uint32_t width,
    uint32_t height,
    uint32_t requestedQuality,
    ByteBuffer& outImage,
    std::string& outImageFormat,
    std::string& errorMessage
)
{
    if (useLosslessImage(requestedQuality)) {
        outImageFormat = "PNG";
        return encodePixelsToPng(pixels, width, height, outImage, errorMessage);
    }

    outImageFormat = "JPG";
    return encodePixelsToJpeg(pixels, width, height, requestedQuality, outImage, errorMessage);
}

bool capturePrimaryScreenPixels(
    CapturedScreenFrame& outFrame,
    FrameTiming& timing,
    std::string& errorMessage
)
{
    const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        errorMessage = "Invalid screen size.";
        return false;
    }

    thread_local ScreenCaptureContext captureContext;
    if (!captureContext.ensureSize(width, height, errorMessage)) {
        return false;
    }

    if (!captureContext.capture(screenX, screenY, outFrame, timing, errorMessage)) {
        return false;
    }

    outFrame.screenWidth = static_cast<uint32_t>(width);
    outFrame.screenHeight = static_cast<uint32_t>(height);
    outFrame.captureWidth = static_cast<uint32_t>(width);
    outFrame.captureHeight = static_cast<uint32_t>(height);
    errorMessage.clear();
    return true;
}

bool capturePrimaryScreenJpeg(
    uint32_t quality,
    uint32_t& outScreenWidth,
    uint32_t& outScreenHeight,
    ByteBuffer& outImage,
    std::string& outImageFormat,
    std::string& errorMessage
)
{
    CapturedScreenFrame frame{};
    FrameTiming timing{};
    if (!capturePrimaryScreenPixels(frame, timing, errorMessage)) {
        return false;
    }

    outScreenWidth = frame.screenWidth;
    outScreenHeight = frame.screenHeight;
    return encodePixelsToScreenImage(
        frame.pixels,
        frame.captureWidth,
        frame.captureHeight,
        quality,
        outImage,
        outImageFormat,
        errorMessage
    );
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

std::vector<ChangedRect> findChangedBlocksInRows(
    const CapturedScreenFrame& previous,
    const CapturedScreenFrame& current,
    uint32_t startY,
    uint32_t endY
)
{
    std::vector<ChangedRect> blocks;
    for (uint32_t y = startY; y < endY; y += DELTA_BLOCK_SIZE) {
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

std::vector<ChangedRect> findChangedBlocksParallel(const CapturedScreenFrame& previous, const CapturedScreenFrame& current)
{
    if (previous.captureWidth != current.captureWidth
        || previous.captureHeight != current.captureHeight
        || previous.pixels.size() != current.pixels.size()) {
        return {{0, 0, current.captureWidth, current.captureHeight}};
    }

    const size_t blockRows = (current.captureHeight + DELTA_BLOCK_SIZE - 1) / DELTA_BLOCK_SIZE;
    const size_t workerCount = screenWorkerCount();
    if (blockRows < MIN_BLOCK_ROWS_PER_DIFF_TASK || workerCount <= 1) {
        return findChangedBlocks(previous, current);
    }

    const size_t taskCount = (std::min)(workerCount, blockRows);
    const size_t rowsPerTask = (blockRows + taskCount - 1) / taskCount;

    std::vector<std::future<std::vector<ChangedRect>>> futures;
    futures.reserve(taskCount);
    for (size_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
        const uint32_t startY = static_cast<uint32_t>(taskIndex * rowsPerTask * DELTA_BLOCK_SIZE);
        if (startY >= current.captureHeight) {
            break;
        }

        const uint32_t endY = (std::min)(
            current.captureHeight,
            static_cast<uint32_t>((taskIndex + 1) * rowsPerTask * DELTA_BLOCK_SIZE)
        );
        futures.push_back(screenProcessingPool().enqueue([&previous, &current, startY, endY]() {
            return findChangedBlocksInRows(previous, current, startY, endY);
        }));
    }

    std::vector<ChangedRect> blocks;
    for (auto& future : futures) {
        std::vector<ChangedRect> partial = future.get();
        blocks.insert(blocks.end(), partial.begin(), partial.end());
    }

    return blocks;
}

std::vector<ChangedRect> mergeChangedBlocks(const std::vector<ChangedRect>& blocks)
{
    if (blocks.empty()) {
        return {};
    }

    std::vector<ChangedRect> horizontalRuns;
    horizontalRuns.reserve(blocks.size());

    for (const ChangedRect& block : blocks) {
        if (!horizontalRuns.empty()) {
            ChangedRect& last = horizontalRuns.back();
            const bool sameRow = last.y == block.y && last.height == block.height;
            const bool touchesRightEdge = last.x + last.width == block.x;
            if (sameRow && touchesRightEdge) {
                last.width += block.width;
                continue;
            }
        }

        horizontalRuns.push_back(block);
    }

    std::vector<ChangedRect> merged;
    merged.reserve(horizontalRuns.size());

    for (const ChangedRect& run : horizontalRuns) {
        bool mergedIntoExisting = false;
        for (ChangedRect& candidate : merged) {
            const bool sameColumnSpan = candidate.x == run.x && candidate.width == run.width;
            const bool touchesBottomEdge = candidate.y + candidate.height == run.y;
            if (sameColumnSpan && touchesBottomEdge) {
                candidate.height += run.height;
                mergedIntoExisting = true;
                break;
            }
        }

        if (!mergedIntoExisting) {
            merged.push_back(run);
        }
    }

    return merged;
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

uint64_t rectArea(const ChangedRect& rect)
{
    return static_cast<uint64_t>(rect.width) * rect.height;
}

bool isLargeComparedToFrame(const ChangedRect& rect, const CapturedScreenFrame& frame, uint32_t percent)
{
    const uint64_t frameArea = static_cast<uint64_t>(frame.captureWidth) * frame.captureHeight;
    if (frameArea == 0) {
        return true;
    }

    return rectArea(rect) * 100 >= frameArea * percent;
}

std::vector<ChangedRect> chooseDeltaRects(const std::vector<ChangedRect>& changedRects, const CapturedScreenFrame& frame)
{
    if (changedRects.size() <= MAX_DELTA_ENCODE_RECTS) {
        return changedRects;
    }

    const ChangedRect bounds = boundingRect(changedRects);
    if (bounds.width == 0 || bounds.height == 0) {
        return {};
    }

    if (isLargeComparedToFrame(bounds, frame, MAX_BOUNDING_RECT_AREA_PERCENT)) {
        return {{0, 0, frame.captureWidth, frame.captureHeight}};
    }

    return {bounds};
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

EncodedRectResult encodeChangedRect(
    const CapturedScreenFrame& frame,
    const ChangedRect& changedRect,
    uint32_t quality,
    std::string& imageFormat,
    std::string& errorMessage
)
{
    EncodedRectResult result{};
    result.rect = changedRect;

    const ByteBuffer rectPixels = copyRectPixels(frame, changedRect);
    if (!encodePixelsToScreenImage(
            rectPixels,
            changedRect.width,
            changedRect.height,
            quality,
            result.image,
            imageFormat,
            errorMessage
        )) {
        return {};
    }

    return result;
}

bool encodeChangedRectsParallel(
    const CapturedScreenFrame& frame,
    const std::vector<ChangedRect>& changedRects,
    uint32_t quality,
    std::vector<EncodedRectResult>& outResults,
    std::string& outImageFormat,
    std::string& errorMessage
)
{
    outResults.clear();
    outImageFormat.clear();

    std::vector<ChangedRect> rectsToEncode;
    rectsToEncode.reserve(changedRects.size());
    for (const ChangedRect& changedRect : changedRects) {
        if (changedRect.width > 0 && changedRect.height > 0) {
            rectsToEncode.push_back(changedRect);
        }
    }

    if (rectsToEncode.empty()) {
        outImageFormat = useLosslessImage(quality) ? "PNG" : "JPG";
        errorMessage.clear();
        return true;
    }

    outResults.resize(rectsToEncode.size());
    if (rectsToEncode.size() < MIN_RECTS_FOR_PARALLEL_ENCODE) {
        for (size_t i = 0; i < rectsToEncode.size(); ++i) {
            std::string imageFormat;
            EncodedRectResult result = encodeChangedRect(frame, rectsToEncode[i], quality, imageFormat, errorMessage);
            if (!errorMessage.empty()) {
                return false;
            }
            outImageFormat = imageFormat;
            outResults[i] = std::move(result);
        }
        errorMessage.clear();
        return true;
    }

    std::vector<std::future<EncodedRectResult>> futures;
    futures.reserve(rectsToEncode.size());
    for (const ChangedRect& rect : rectsToEncode) {
        futures.push_back(screenProcessingPool().enqueue([&frame, rect, quality]() {
            std::string imageFormat;
            std::string localError;
            EncodedRectResult result = encodeChangedRect(frame, rect, quality, imageFormat, localError);
            if (!localError.empty()) {
                throw std::runtime_error(localError);
            }
            return result;
        }));
    }

    try {
        for (size_t i = 0; i < futures.size(); ++i) {
            outResults[i] = futures[i].get();
        }
    } catch (const std::exception& ex) {
        errorMessage = ex.what();
        return false;
    }

    outImageFormat = useLosslessImage(quality) ? "PNG" : "JPG";
    errorMessage.clear();
    return true;
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

    if (payload.empty()) {
        return request;
    }

    if (!deserializeScreenshotStartRequest(payload, request)) {
        request.quality = 70;
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
    request.intervalMs = DEFAULT_STREAM_INTERVAL_MS;

    if (payload.empty()) {
        return request;
    }

    if (!deserializeScreenStreamStartRequest(payload, request)) {
        request.quality = 70;
        request.intervalMs = DEFAULT_STREAM_INTERVAL_MS;
    }

    return request;
}

StreamControlResult waitForStreamControlOrTimeout(SOCKET clientSock, std::chrono::milliseconds timeout)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(clientSock, &readSet);

    timeval waitTime{};
    waitTime.tv_sec = static_cast<long>(timeout.count() / 1000);
    waitTime.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    const int ready = select(0, &readSet, nullptr, nullptr, &waitTime);
    if (ready <= 0) {
        return {};
    }

    ParsedPacket packet{};
    if (!recvPacket(clientSock, packet)) {
        return {StreamControlEvent::Stop, 0};
    }

    if (packet.header.command == CMD::CMD_SCREEN_STREAM_STOP) {
        return {StreamControlEvent::Stop, 0};
    }

    if (packet.header.command == CMD::CMD_SCREEN_STREAM_KEYFRAME_REQUEST) {
        return {StreamControlEvent::KeyFrameRequest, 0};
    }

    if (packet.header.command == CMD::CMD_SCREEN_STREAM_FRAME_ACK) {
        ScreenStreamFrameAck ack{};
        if (!deserializeScreenStreamFrameAck(packet.payload, ack)) {
            return {StreamControlEvent::Stop, 0};
        }

        if (ack.ok == 0) {
            return {StreamControlEvent::KeyFrameRequest, ack.frameId};
        }

        return {StreamControlEvent::FrameAck, ack.frameId};
    }

    return {};
}

} // namespace

bool handleScreenshotStart(SOCKET clientSock, const ByteBuffer& requestPayload)
{
    ByteBuffer image;
    std::string imageFormat;
    std::string errorMessage;
    const ScreenshotStartRequest request = parseScreenshotRequest(requestPayload);
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    if (!capturePrimaryScreenJpeg(request.quality, screenWidth, screenHeight, image, imageFormat, errorMessage)) {
        return sendScreenshotStartError(clientSock, errorMessage);
    }

    ScreenshotStartResponse response{};
    response.ok = 1;
    response.imageSize = static_cast<uint64_t>(image.size());
    response.screenWidth = screenWidth;
    response.screenHeight = screenHeight;
    response.fileName = imageFormat == "PNG" ? "screenshot.png" : "screenshot.jpg";
    response.imageFormat = imageFormat;

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
    CapturedScreenFrame currentFrame{};
    uint64_t previousFrameId = 0;
    uint64_t nextFrameId = 1;
    uint64_t lastKeyFrameImageSize = 0;
    uint32_t previousSendMs = 0;
    uint32_t previousAckWaitMs = 0;
    bool forceKeyFrame = false;

    while (true) {
        const auto frameStartedAt = std::chrono::steady_clock::now();
        ByteBuffer image;
        std::string imageFormat;
        std::string errorMessage;
        FrameTiming timing{};

        auto stageStartedAt = std::chrono::steady_clock::now();
        if (!capturePrimaryScreenPixels(currentFrame, timing, errorMessage)) {
            return sendPacket(clientSock, CMD::CMD_ERROR, errorMessage);
        }
        timing.captureMs = elapsedMsSince(stageStartedAt);

        bool sendKeyFrame = forceKeyFrame
            || previousFrame.pixels.empty()
            || ((nextFrameId - 1) % KEY_FRAME_INTERVAL == 0);

        stageStartedAt = std::chrono::steady_clock::now();
        std::vector<ChangedRect> changedRects = sendKeyFrame
            ? std::vector<ChangedRect>{{0, 0, currentFrame.captureWidth, currentFrame.captureHeight}}
            : findChangedBlocksParallel(previousFrame, currentFrame);
        if (!sendKeyFrame) {
            changedRects = mergeChangedBlocks(changedRects);
            changedRects = chooseDeltaRects(changedRects, currentFrame);
        }
        timing.compareMs = elapsedMsSince(stageStartedAt);

        bool fallbackToKeyFrame = false;
        if (!sendKeyFrame && (changedRects.size() > MAX_DELTA_RECTS
                || (changedRects.size() == 1
                    && changedRects.front().x == 0
                    && changedRects.front().y == 0
                    && changedRects.front().width == currentFrame.captureWidth
                    && changedRects.front().height == currentFrame.captureHeight))) {
            sendKeyFrame = true;
            fallbackToKeyFrame = true;
            changedRects = {{0, 0, currentFrame.captureWidth, currentFrame.captureHeight}};
        }

        stageStartedAt = std::chrono::steady_clock::now();
        std::vector<ScreenStreamRect> streamRects;
        std::vector<EncodedRectResult> encodedRects;
        if (!encodeChangedRectsParallel(
                currentFrame,
                changedRects,
                request.quality,
                encodedRects,
                imageFormat,
                errorMessage
            )) {
            return sendPacket(clientSock, CMD::CMD_ERROR, errorMessage);
        }

        streamRects.reserve(encodedRects.size());
        for (const EncodedRectResult& encodedRect : encodedRects) {
            streamRects.push_back({
                encodedRect.rect.x,
                encodedRect.rect.y,
                encodedRect.rect.width,
                encodedRect.rect.height,
                static_cast<uint64_t>(encodedRect.image.size())
            });
            image.insert(image.end(), encodedRect.image.begin(), encodedRect.image.end());
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
        frameHeader.bltMs = timing.bltMs;
        frameHeader.copyMs = timing.copyMs;
        frameHeader.compareMs = timing.compareMs;
        frameHeader.encodeMs = timing.encodeMs;
        frameHeader.sendMs = previousSendMs;
        frameHeader.ackWaitMs = previousAckWaitMs;
        frameHeader.fallbackToKeyFrame = fallbackToKeyFrame ? 1u : 0u;
        frameHeader.rects = std::move(streamRects);
        frameHeader.imageFormat = imageFormat.empty() ? "JPG" : imageFormat;

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

        stageStartedAt = std::chrono::steady_clock::now();
        const StreamControlResult ackResult = waitForStreamControlOrTimeout(
            clientSock,
            std::chrono::milliseconds(FRAME_ACK_TIMEOUT_MS)
        );
        timing.ackWaitMs = elapsedMsSince(stageStartedAt);
        previousAckWaitMs = timing.ackWaitMs;
        if (ackResult.event == StreamControlEvent::Stop) {
            return true;
        }
        if (ackResult.event == StreamControlEvent::KeyFrameRequest) {
            previousFrame = {};
            previousFrameId = 0;
            ++nextFrameId;
            forceKeyFrame = true;
            continue;
        }
        if (ackResult.event != StreamControlEvent::FrameAck || ackResult.frameId != nextFrameId) {
            return false;
        }

        std::swap(previousFrame, currentFrame);
        previousFrameId = nextFrameId;
        ++nextFrameId;
        forceKeyFrame = false;

        const auto frameInterval = std::chrono::milliseconds(intervalMs);
        const auto elapsed = std::chrono::steady_clock::now() - frameStartedAt;
        if (elapsed < frameInterval) {
            const StreamControlResult result = waitForStreamControlOrTimeout(
                    clientSock,
                    std::chrono::duration_cast<std::chrono::milliseconds>(frameInterval - elapsed)
                );
            if (result.event == StreamControlEvent::Stop) {
                return true;
            }
            if (result.event == StreamControlEvent::KeyFrameRequest) {
                forceKeyFrame = true;
            }
            continue;
        }

        const StreamControlResult result = waitForStreamControlOrTimeout(clientSock, std::chrono::milliseconds(0));
        if (result.event == StreamControlEvent::Stop) {
            return true;
        }
        if (result.event == StreamControlEvent::KeyFrameRequest) {
            forceKeyFrame = true;
        }
    }
}
