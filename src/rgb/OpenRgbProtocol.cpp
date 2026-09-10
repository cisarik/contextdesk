#include "rgb/OpenRgbProtocol.h"

#include <QtEndian>

#include <cstring>

namespace contextdeck::openrgb {
namespace {

void appendU16(QByteArray &out, quint16 value)
{
    const quint16 le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char *>(&le), sizeof(le));
}

void appendU32(QByteArray &out, quint32 value)
{
    const quint32 le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char *>(&le), sizeof(le));
}

bool readU16(QByteArrayView bytes, int &offset, quint16 &value, DecodeError *error)
{
    if (offset < 0 || bytes.size() - offset < static_cast<qsizetype>(sizeof(quint16))) {
        if (error) {
            error->reason = QStringLiteral("truncated uint16");
        }
        return false;
    }
    quint16 le = 0;
    std::memcpy(&le, bytes.constData() + offset, sizeof(le));
    value = qFromLittleEndian(le);
    offset += static_cast<int>(sizeof(quint16));
    return true;
}

bool readU32(QByteArrayView bytes, int &offset, quint32 &value, DecodeError *error)
{
    if (offset < 0 || bytes.size() - offset < static_cast<qsizetype>(sizeof(quint32))) {
        if (error) {
            error->reason = QStringLiteral("truncated uint32");
        }
        return false;
    }
    quint32 le = 0;
    std::memcpy(&le, bytes.constData() + offset, sizeof(le));
    value = qFromLittleEndian(le);
    offset += static_cast<int>(sizeof(quint32));
    return true;
}

bool readI32(QByteArrayView bytes, int &offset, qint32 &value, DecodeError *error)
{
    quint32 raw = 0;
    if (!readU32(bytes, offset, raw, error)) {
        return false;
    }
    value = static_cast<qint32>(raw);
    return true;
}

bool readCountedString(QByteArrayView bytes, int &offset, QString &value, bool store, DecodeError *error)
{
    quint16 length = 0;
    if (!readU16(bytes, offset, length, error)) {
        return false;
    }
    if (length == 0 || length > 1024) {
        if (error) {
            error->reason = QStringLiteral("counted string length out of bounds");
        }
        return false;
    }
    if (bytes.size() - offset < length) {
        if (error) {
            error->reason = QStringLiteral("truncated counted string");
        }
        return false;
    }
    const char *data = bytes.constData() + offset;
    if (data[length - 1] != '\0') {
        if (error) {
            error->reason = QStringLiteral("counted string is not NUL-terminated");
        }
        return false;
    }
    if (store) {
        value = QString::fromUtf8(data, length - 1);
    }
    offset += length;
    return true;
}

void setError(DecodeError *error, const QString &reason)
{
    if (error) {
        error->reason = reason;
    }
}

} // namespace

QByteArray encodeHeader(const PacketHeader &header)
{
    QByteArray out;
    out.reserve(kHeaderSize);
    out.append(kMagic, 4);
    appendU32(out, header.deviceIndex);
    appendU32(out, static_cast<quint32>(header.packetId));
    appendU32(out, header.payloadSize);
    return out;
}

std::optional<PacketHeader> decodeHeader(QByteArrayView bytes, DecodeError *error)
{
    if (bytes.size() < kHeaderSize) {
        setError(error, QStringLiteral("truncated frame header"));
        return std::nullopt;
    }
    if (std::memcmp(bytes.constData(), kMagic, 4) != 0) {
        setError(error, QStringLiteral("wrong magic"));
        return std::nullopt;
    }
    int offset = 4;
    PacketHeader header;
    quint32 packetId = 0;
    if (!readU32(bytes, offset, header.deviceIndex, error) || !readU32(bytes, offset, packetId, error)
        || !readU32(bytes, offset, header.payloadSize, error)) {
        return std::nullopt;
    }
    if (header.payloadSize > kMaxPayloadBytes) {
        setError(error, QStringLiteral("payload exceeds 1 MiB cap"));
        return std::nullopt;
    }
    header.packetId = static_cast<PacketId>(packetId);
    return header;
}

QByteArray encodePacket(const PacketHeader &header, QByteArrayView payload)
{
    PacketHeader copy = header;
    copy.payloadSize = static_cast<quint32>(payload.size());
    QByteArray out = encodeHeader(copy);
    out.append(payload.constData(), payload.size());
    return out;
}

QByteArray encodeProtocolVersionRequest()
{
    QByteArray payload;
    appendU32(payload, kProtocolVersion);
    PacketHeader header;
    header.packetId = PacketId::RequestProtocolVersion;
    header.payloadSize = static_cast<quint32>(payload.size());
    return encodePacket(header, payload);
}

std::optional<quint32> decodeUint32Payload(QByteArrayView payload, DecodeError *error)
{
    if (payload.size() != static_cast<qsizetype>(sizeof(quint32))) {
        setError(error, QStringLiteral("uint32 payload must be 4 bytes"));
        return std::nullopt;
    }
    int offset = 0;
    quint32 value = 0;
    if (!readU32(payload, offset, value, error)) {
        return std::nullopt;
    }
    return value;
}

std::optional<quint32> decodeProtocolVersionPayload(QByteArrayView payload, DecodeError *error)
{
    return decodeUint32Payload(payload, error);
}

bool protocolVersionAcceptable(quint32 serverVersion)
{
    return serverVersion > 0 && serverVersion <= kProtocolVersion;
}

QByteArray encodeClientName(const QString &name)
{
    const QByteArray utf8 = name.toUtf8();
    QByteArray payload = utf8;
    payload.append('\0');
    PacketHeader header;
    header.packetId = PacketId::SetClientName;
    return encodePacket(header, payload);
}

QByteArray encodeControllerDataRequest(quint32 deviceIndex, quint32 protocolVersion)
{
    QByteArray payload;
    appendU32(payload, protocolVersion);
    PacketHeader header;
    header.deviceIndex = deviceIndex;
    header.packetId = PacketId::RequestControllerData;
    return encodePacket(header, payload);
}

QByteArray encodeSetCustomMode(quint32 deviceIndex)
{
    PacketHeader header;
    header.deviceIndex = deviceIndex;
    header.packetId = PacketId::SetCustomMode;
    header.payloadSize = 0;
    return encodeHeader(header);
}

quint32 rgbToOpenRgb(const Rgb &color)
{
    return static_cast<quint32>(color.b) << 16 | static_cast<quint32>(color.g) << 8 | static_cast<quint32>(color.r);
}

Rgb openRgbToRgb(quint32 value)
{
    Rgb color;
    color.r = static_cast<quint8>(value & 0xffu);
    color.g = static_cast<quint8>((value >> 8) & 0xffu);
    color.b = static_cast<quint8>((value >> 16) & 0xffu);
    return color;
}

QByteArray encodeUpdateLeds(quint32 deviceIndex, const std::array<Rgb, kLedCount> &colors)
{
    const quint16 numColors = static_cast<quint16>(kLedCount);
    const quint32 dataSize = static_cast<quint32>(sizeof(quint32) + sizeof(quint16) + kLedCount * sizeof(quint32));
    QByteArray payload;
    appendU32(payload, dataSize);
    appendU16(payload, numColors);
    for (const Rgb &color : colors) {
        appendU32(payload, rgbToOpenRgb(color));
    }
    PacketHeader header;
    header.deviceIndex = deviceIndex;
    header.packetId = PacketId::UpdateLeds;
    return encodePacket(header, payload);
}

std::optional<std::array<Rgb, kLedCount>> decodeUpdateLedsPayload(QByteArrayView payload, DecodeError *error)
{
    int offset = 0;
    quint32 dataSize = 0;
    quint16 numColors = 0;
    if (!readU32(payload, offset, dataSize, error) || !readU16(payload, offset, numColors, error)) {
        return std::nullopt;
    }
    if (numColors != kLedCount) {
        setError(error, QStringLiteral("UPDATELEDS color count must be 5"));
        return std::nullopt;
    }
    const quint32 expected = static_cast<quint32>(sizeof(quint32) + sizeof(quint16) + kLedCount * sizeof(quint32));
    if (dataSize != expected || payload.size() != static_cast<qsizetype>(expected)) {
        setError(error, QStringLiteral("UPDATELEDS payload size mismatch"));
        return std::nullopt;
    }
    std::array<Rgb, kLedCount> colors{};
    for (quint32 i = 0; i < kLedCount; ++i) {
        quint32 value = 0;
        if (!readU32(payload, offset, value, error)) {
            return std::nullopt;
        }
        colors[i] = openRgbToRgb(value);
    }
    return colors;
}

std::optional<ControllerIdentity> parseControllerIdentity(QByteArrayView payload, quint32 protocolVersion,
                                                          DecodeError *error)
{
    int offset = 0;
    quint32 declaredSize = 0;
    if (!readU32(payload, offset, declaredSize, error)) {
        return std::nullopt;
    }
    if (declaredSize > kMaxPayloadBytes || static_cast<qsizetype>(declaredSize) > payload.size()) {
        setError(error, QStringLiteral("controller data size out of bounds"));
        return std::nullopt;
    }
    ControllerIdentity identity;
    if (!readI32(payload, offset, identity.deviceType, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, identity.name, true, error)) {
        return std::nullopt;
    }
    if (protocolVersion >= 1 && !readCountedString(payload, offset, identity.vendor, true, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, identity.description, true, error)) {
        return std::nullopt;
    }
    QString ignored;
    if (!readCountedString(payload, offset, ignored, false, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, ignored, false, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, identity.location, true, error)) {
        return std::nullopt;
    }
    return identity;
}

bool isLogitechG213(const ControllerIdentity &identity)
{
    const auto containsCi = [](const QString &haystack, const QString &needle) {
        return haystack.contains(needle, Qt::CaseInsensitive);
    };
    if (!containsCi(identity.vendor, QStringLiteral("Logitech")) && !containsCi(identity.name, QStringLiteral("Logitech"))
        && !containsCi(identity.description, QStringLiteral("Logitech"))) {
        return false;
    }
    if (!containsCi(identity.name, QStringLiteral("G213")) && !containsCi(identity.description, QStringLiteral("G213"))) {
        return false;
    }
    if (!identity.location.isEmpty()) {
        const QString lowered = identity.location.toLower();
        if (lowered.contains(QLatin1String("046d")) && !(lowered.contains(QLatin1String("c336")))) {
            return false;
        }
    }
    return true;
}

} // namespace contextdeck::openrgb
