#include "protocol/PacketCodec.h"

#include <cstring>

#include "protocol/BinaryReader.h"
#include "protocol/BinaryWriter.h"

#include <windows.h>
#include <wincrypt.h>

ByteBuffer PacketCodec::stringToBytes(const std::string& text)
{
    return ByteBuffer(text.begin(), text.end());
}

std::string PacketCodec::bytesToString(const ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

ByteBuffer PacketCodec::pack(CMD::Type command, const ByteBuffer& payload)
{
    uint32_t totalLen = static_cast<uint32_t>(PACKET_HEADER_SIZE + payload.size());

    PacketHeader header{};
    header.signature = PACKET_SIGNATURE;
    header.length = totalLen;
    header.command = command;
    calculateMD5(payload, header.md5_hash);

    ByteBuffer encryptedPayload = payload;
    if (!encryptedPayload.empty()) {
        xorCrypt(encryptedPayload);
    }

    ByteBuffer fullPacket = encodeHeader(header);

    if (!encryptedPayload.empty()) {
        fullPacket.insert(fullPacket.end(), encryptedPayload.begin(), encryptedPayload.end());
    }

    return fullPacket;
}

ByteBuffer PacketCodec::pack(CMD::Type command, const std::string& text)
{
    return pack(command, stringToBytes(text));
}

bool PacketCodec::decodeHeader(const ByteBuffer& bytes, PacketHeader& outHeader)
{
    if (bytes.size() != PACKET_HEADER_SIZE) {
        return false;
    }

    BinaryReader reader(bytes);
    uint16_t command = 0;

    if (!reader.readUint16(outHeader.signature)
        || !reader.readUint32(outHeader.length)
        || !reader.readUint16(command)
        || !reader.readBytes(outHeader.md5_hash, sizeof(outHeader.md5_hash))
        || !reader.isFinished()) {
        return false;
    }

    outHeader.command = static_cast<CMD::Type>(command);
    return true;
}

bool PacketCodec::unpack(const PacketHeader& header, const ByteBuffer& payload, ParsedPacket& outPacket)
{
    if (header.signature != PACKET_SIGNATURE) {
        return false;
    }

    if (header.length < PACKET_HEADER_SIZE) {
        return false;
    }

    if (header.length != PACKET_HEADER_SIZE + payload.size()) {
        return false;
    }

    ByteBuffer decryptedPayload = payload;
    if (!decryptedPayload.empty()) {
        xorCrypt(decryptedPayload);
    }

    uint8_t calculatedMD5[16]{};
    calculateMD5(decryptedPayload, calculatedMD5);

    if (std::memcmp(calculatedMD5, header.md5_hash, 16) != 0) {
        return false;
    }

    outPacket.header = header;
    outPacket.payload = decryptedPayload;
    return true;
}

ByteBuffer PacketCodec::encodeHeader(const PacketHeader& header)
{
    BinaryWriter writer;
    writer.writeUint16(header.signature);
    writer.writeUint32(header.length);
    writer.writeUint16(static_cast<uint16_t>(header.command));
    writer.writeBytes(header.md5_hash, sizeof(header.md5_hash));
    return writer.buffer();
}

void PacketCodec::xorCrypt(ByteBuffer& data)
{
    const std::string key = "MySecretRemoteControlKey";

    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % key.size()];
    }
}

void PacketCodec::calculateMD5(const ByteBuffer& data, uint8_t outHash[16])
{
    std::memset(outHash, 0, 16);

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            if (data.empty() || CryptHashData(hHash, data.data(), static_cast<DWORD>(data.size()), 0)) {
                DWORD hashLen = 16;
                CryptGetHashParam(hHash, HP_HASHVAL, outHash, &hashLen, 0);
            }

            CryptDestroyHash(hHash);
        }

        CryptReleaseContext(hProv, 0);
    }
}
