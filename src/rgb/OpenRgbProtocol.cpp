#include "rgb/OpenRgbProtocol.h"

#include <QtEndian>

#include <algorithm>
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

quint32 clampModeSpeed(const ControllerMode &mode, quint32 speed)
{
    const quint32 lo = std::min(mode.speedMin, mode.speedMax);
    const quint32 hi = std::max(mode.speedMin, mode.speedMax);
    if (lo >= hi) {
        return lo;
    }
    return std::clamp(speed, lo, hi);
}

ControllerMode modeForDesiredUpdate(const ControllerMode &source, const DesiredLighting &desired)
{
    ControllerMode modeCopy = source;
    if (desired.mode == LightingMode::Breathing) {
        modeCopy.colors.resize(1);
        modeCopy.colors[0] = desired.baseColor.value_or(kDefaultEffectColor);
    }
    const bool animated = desired.mode == LightingMode::Wave || desired.mode == LightingMode::Cycle
        || desired.mode == LightingMode::Breathing;
    if (animated && desired.speed.has_value()) {
        modeCopy.speed = clampModeSpeed(modeCopy, *desired.speed);
    }
    return modeCopy;
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

void appendCountedString(QByteArray &out, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    const quint16 length = static_cast<quint16>(utf8.size() + 1);
    appendU16(out, length);
    out.append(utf8);
    out.append('\0');
}

QByteArray encodeModeData(const ControllerMode &mode, quint32 protocolVersion)
{
    QByteArray out;
    appendCountedString(out, mode.name);
    appendU32(out, static_cast<quint32>(mode.value));
    appendU32(out, mode.flags);
    appendU32(out, mode.speedMin);
    appendU32(out, mode.speedMax);
    if (protocolVersion >= 3) {
        appendU32(out, mode.brightnessMin);
        appendU32(out, mode.brightnessMax);
    }
    appendU32(out, mode.colorsMin);
    appendU32(out, mode.colorsMax);
    appendU32(out, mode.speed);
    if (protocolVersion >= 3) {
        appendU32(out, mode.brightness);
    }
    appendU32(out, mode.direction);
    appendU32(out, mode.colorMode);
    const quint16 numColors = static_cast<quint16>(mode.colors.size());
    appendU16(out, numColors);
    for (const Rgb &color : mode.colors) {
        appendU32(out, rgbToOpenRgb(color));
    }
    return out;
}

std::optional<ControllerMode> decodeModeData(QByteArrayView bytes, int &offset, quint32 protocolVersion,
                                             DecodeError *error)
{
    ControllerMode mode;
    if (!readCountedString(bytes, offset, mode.name, true, error)) {
        return std::nullopt;
    }
    quint32 value = 0;
    if (!readU32(bytes, offset, value, error) || !readU32(bytes, offset, mode.flags, error)
        || !readU32(bytes, offset, mode.speedMin, error) || !readU32(bytes, offset, mode.speedMax, error)) {
        return std::nullopt;
    }
    mode.value = static_cast<qint32>(value);
    if (protocolVersion >= 3
        && (!readU32(bytes, offset, mode.brightnessMin, error) || !readU32(bytes, offset, mode.brightnessMax, error))) {
        return std::nullopt;
    }
    if (!readU32(bytes, offset, mode.colorsMin, error) || !readU32(bytes, offset, mode.colorsMax, error)
        || !readU32(bytes, offset, mode.speed, error)) {
        return std::nullopt;
    }
    if (protocolVersion >= 3 && !readU32(bytes, offset, mode.brightness, error)) {
        return std::nullopt;
    }
    if (!readU32(bytes, offset, mode.direction, error) || !readU32(bytes, offset, mode.colorMode, error)) {
        return std::nullopt;
    }
    quint16 numColors = 0;
    if (!readU16(bytes, offset, numColors, error)) {
        return std::nullopt;
    }
    if (numColors > 32) {
        setError(error, QStringLiteral("mode color count exceeds bound"));
        return std::nullopt;
    }
    mode.colors.reserve(numColors);
    for (quint16 i = 0; i < numColors; ++i) {
        quint32 raw = 0;
        if (!readU32(bytes, offset, raw, error)) {
            return std::nullopt;
        }
        mode.colors.push_back(openRgbToRgb(raw));
    }
    return mode;
}

QByteArray encodeUpdateMode(quint32 deviceIndex, int modeIndex, const ControllerMode &mode, quint32 protocolVersion)
{
    const QByteArray modeData = encodeModeData(mode, protocolVersion);
    const quint32 dataSize = static_cast<quint32>(sizeof(quint32) + sizeof(qint32) + modeData.size());
    QByteArray payload;
    appendU32(payload, dataSize);
    appendU32(payload, static_cast<quint32>(modeIndex));
    payload.append(modeData);
    PacketHeader header;
    header.deviceIndex = deviceIndex;
    header.packetId = PacketId::UpdateMode;
    return encodePacket(header, payload);
}

QString openRgbModeName(LightingMode mode)
{
    switch (mode) {
    case LightingMode::Direct:
        return QStringLiteral("Direct");
    case LightingMode::Wave:
        return QStringLiteral("Wave");
    case LightingMode::Cycle:
        return QStringLiteral("Cycle");
    case LightingMode::Breathing:
        return QStringLiteral("Breathing");
    case LightingMode::Off:
        return QStringLiteral("Off");
    case LightingMode::Untouched:
        return {};
    }
    return {};
}

std::optional<LightingMode> lightingModeFromOpenRgbName(QStringView name)
{
    if (name.compare(QLatin1String("Direct"), Qt::CaseInsensitive) == 0) {
        return LightingMode::Direct;
    }
    if (name.compare(QLatin1String("Wave"), Qt::CaseInsensitive) == 0) {
        return LightingMode::Wave;
    }
    if (name.compare(QLatin1String("Cycle"), Qt::CaseInsensitive) == 0) {
        return LightingMode::Cycle;
    }
    if (name.compare(QLatin1String("Breathing"), Qt::CaseInsensitive) == 0) {
        return LightingMode::Breathing;
    }
    if (name.compare(QLatin1String("Off"), Qt::CaseInsensitive) == 0) {
        return LightingMode::Off;
    }
    return std::nullopt;
}

std::optional<int> findModeIndex(const QVector<ControllerMode> &modes, LightingMode mode)
{
    const QString name = openRgbModeName(mode);
    if (name.isEmpty()) {
        return std::nullopt;
    }
    for (int i = 0; i < modes.size(); ++i) {
        if (modes.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<QVector<QByteArray>> encodeDesiredStateFrames(quint32 deviceIndex, const DesiredLighting &desired,
                                                            const QVector<ControllerMode> &modes,
                                                            quint32 protocolVersion, DecodeError *error)
{
    if (desired.mode == LightingMode::Untouched) {
        return QVector<QByteArray>{};
    }
    const auto index = findModeIndex(modes, desired.mode);
    if (!index) {
        setError(error, QStringLiteral("requested lighting mode is not on the controller"));
        return std::nullopt;
    }
    QVector<QByteArray> frames;
    frames.push_back(encodeUpdateMode(deviceIndex, *index, modeForDesiredUpdate(modes.at(*index), desired),
                                      protocolVersion));
    if (desired.mode == LightingMode::Direct) {
        frames.push_back(encodeUpdateLeds(deviceIndex, desired.colors));
    }
    return frames;
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

std::optional<ControllerSnapshot> parseControllerSnapshot(QByteArrayView payload, quint32 protocolVersion,
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
    ControllerSnapshot snapshot;
    if (!readI32(payload, offset, snapshot.identity.deviceType, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, snapshot.identity.name, true, error)) {
        return std::nullopt;
    }
    if (protocolVersion >= 1 && !readCountedString(payload, offset, snapshot.identity.vendor, true, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, snapshot.identity.description, true, error)) {
        return std::nullopt;
    }
    QString ignored;
    if (!readCountedString(payload, offset, ignored, false, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, ignored, false, error)) {
        return std::nullopt;
    }
    if (!readCountedString(payload, offset, snapshot.identity.location, true, error)) {
        return std::nullopt;
    }

    quint16 numModes = 0;
    quint32 activeModeRaw = 0;
    if (!readU16(payload, offset, numModes, error) || !readU32(payload, offset, activeModeRaw, error)) {
        return std::nullopt;
    }
    if (numModes > 64) {
        setError(error, QStringLiteral("mode count exceeds bound"));
        return std::nullopt;
    }
    snapshot.activeMode = static_cast<qint32>(activeModeRaw);
    snapshot.modes.reserve(numModes);
    for (quint16 i = 0; i < numModes; ++i) {
        const auto mode = decodeModeData(payload, offset, protocolVersion, error);
        if (!mode) {
            return std::nullopt;
        }
        snapshot.modes.push_back(*mode);
    }
    return snapshot;
}

std::optional<ControllerIdentity> parseControllerIdentity(QByteArrayView payload, quint32 protocolVersion,
                                                          DecodeError *error)
{
    const auto snapshot = parseControllerSnapshot(payload, protocolVersion, error);
    if (!snapshot) {
        return std::nullopt;
    }
    return snapshot->identity;
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
