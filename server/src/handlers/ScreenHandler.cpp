#include "handlers/ScreenHandler.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>

#include "net/SocketHelpers.h"
#include "protocol/Command.h"
#include "protocol/Messages.h"

namespace {

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

bool capturePrimaryScreenJpeg(
    uint32_t quality,
    uint32_t scalePercent,
    uint32_t& outScreenWidth,
    uint32_t& outScreenHeight,
    ByteBuffer& outImage,
    std::string& errorMessage
)
{
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    if (width <= 0 || height <= 0) {
        errorMessage = "Invalid screen size.";
        return false;
    }

    outScreenWidth = static_cast<uint32_t>(width);
    outScreenHeight = static_cast<uint32_t>(height);

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
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (!copied) {
        DeleteObject(bitmap);
        errorMessage = "Screen capture failed.";
        return false;
    }

    const bool encoded = encodeBitmapToJpeg(bitmap, quality, outImage, errorMessage);
    DeleteObject(bitmap);
    return encoded;
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

    constexpr size_t chunkSize = 65536;
    size_t offset = 0;
    while (offset < image.size()) {
        const size_t bytesToSend = (std::min)(chunkSize, image.size() - offset);
        ByteBuffer chunk(image.begin() + offset, image.begin() + offset + bytesToSend);

        if (!sendPacket(clientSock, CMD::CMD_SCREENSHOT_CHUNK, chunk)) {
            return false;
        }

        offset += bytesToSend;
    }

    return sendPacket(clientSock, CMD::CMD_SCREENSHOT_END, ByteBuffer{});
}
