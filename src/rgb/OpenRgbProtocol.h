#pragma once

#include "core/Types.h"

#include <array>
#include <optional>

#include <QByteArray>
#include <QString>

namespace contextdeck::openrgb {

inline constexpr char kMagic[4] = {'O', 'R', 'G', 'B'};
inline constexpr quint16 kDefaultPort = 6742;
inline constexpr quint32 kProtocolVersion = 5;
inline constexpr quint32 kMaxPayloadBytes = 1024 * 1024;
inline constexpr int kHeaderSize = 16;
inline constexpr quint32 kLedCount = 5;

enum class PacketId : quint32 {
    RequestControllerCount = 0,
    RequestControllerData = 1,
    RequestProtocolVersion = 40,
    SetClientName = 50,
    DeviceListUpdated = 100,
    UpdateLeds = 1050,
    SetCustomMode = 1100,
};

struct PacketHeader {
    quint32 deviceIndex = 0;
    PacketId packetId = PacketId::RequestControllerCount;
    quint32 payloadSize = 0;
};

struct DecodeError {
    QString reason;
};

struct ControllerIdentity {
    QString name;
    QString vendor;
    QString description;
    QString location;
    int deviceType = -1;
};

[[nodiscard]] QByteArray encodeHeader(const PacketHeader &header);
[[nodiscard]] std::optional<PacketHeader> decodeHeader(QByteArrayView bytes, DecodeError *error = nullptr);
[[nodiscard]] QByteArray encodePacket(const PacketHeader &header, QByteArrayView payload);

[[nodiscard]] QByteArray encodeProtocolVersionRequest();
[[nodiscard]] std::optional<quint32> decodeProtocolVersionPayload(QByteArrayView payload, DecodeError *error = nullptr);
[[nodiscard]] std::optional<quint32> decodeUint32Payload(QByteArrayView payload, DecodeError *error = nullptr);
[[nodiscard]] bool protocolVersionAcceptable(quint32 serverVersion);

[[nodiscard]] QByteArray encodeClientName(const QString &name);
[[nodiscard]] QByteArray encodeControllerDataRequest(quint32 deviceIndex, quint32 protocolVersion);
[[nodiscard]] QByteArray encodeSetCustomMode(quint32 deviceIndex);
[[nodiscard]] QByteArray encodeUpdateLeds(quint32 deviceIndex, const std::array<Rgb, kLedCount> &colors);
[[nodiscard]] std::optional<std::array<Rgb, kLedCount>> decodeUpdateLedsPayload(QByteArrayView payload,
                                                                               DecodeError *error = nullptr);

[[nodiscard]] quint32 rgbToOpenRgb(const Rgb &color);
[[nodiscard]] Rgb openRgbToRgb(quint32 value);

[[nodiscard]] std::optional<ControllerIdentity> parseControllerIdentity(QByteArrayView payload,
                                                                        quint32 protocolVersion,
                                                                        DecodeError *error = nullptr);
[[nodiscard]] bool isLogitechG213(const ControllerIdentity &identity);

} // namespace contextdeck::openrgb
