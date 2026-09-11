#include "rgb/OpenRgbProtocol.h"

#include <QtEndian>
#include <cstring>

#include <QTest>

using namespace contextdeck;
using namespace contextdeck::openrgb;

class TestOpenRgbProtocol : public QObject
{
    Q_OBJECT

private slots:
    void headerRoundTrip()
    {
        PacketHeader header;
        header.deviceIndex = 3;
        header.packetId = PacketId::UpdateLeds;
        header.payloadSize = 26;
        const QByteArray encoded = encodeHeader(header);
        QCOMPARE(encoded.size(), kHeaderSize);
        QCOMPARE(encoded.left(4), QByteArray("ORGB", 4));

        DecodeError error;
        const auto decoded = decodeHeader(encoded, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.reason));
        QCOMPARE(decoded->deviceIndex, header.deviceIndex);
        QCOMPARE(static_cast<quint32>(decoded->packetId), static_cast<quint32>(header.packetId));
        QCOMPARE(decoded->payloadSize, header.payloadSize);
    }

    void rejectWrongMagic()
    {
        QByteArray bytes = encodeHeader(PacketHeader{});
        bytes[0] = 'X';
        DecodeError error;
        QVERIFY(!decodeHeader(bytes, &error).has_value());
        QCOMPARE(error.reason, QStringLiteral("wrong magic"));
    }

    void rejectTruncatedFrame()
    {
        const QByteArray bytes = encodeHeader(PacketHeader{}).left(8);
        DecodeError error;
        QVERIFY(!decodeHeader(bytes, &error).has_value());
        QVERIFY(error.reason.contains(QStringLiteral("truncated")));
    }

    void rejectOversizedPayload()
    {
        PacketHeader header;
        header.payloadSize = kMaxPayloadBytes + 1;
        const QByteArray bytes = encodeHeader(header);
        DecodeError error;
        QVERIFY(!decodeHeader(bytes, &error).has_value());
        QVERIFY(error.reason.contains(QStringLiteral("1 MiB")));
    }

    void protocolVersionNegotiationRejection()
    {
        const QByteArray request = encodeProtocolVersionRequest();
        QCOMPARE(request.size(), kHeaderSize + 4);
        DecodeError error;
        const auto header = decodeHeader(request, &error);
        QVERIFY(header.has_value());
        QCOMPARE(header->packetId, PacketId::RequestProtocolVersion);
        const auto requested = decodeProtocolVersionPayload(request.mid(kHeaderSize), &error);
        QVERIFY(requested.has_value());
        QCOMPARE(*requested, kProtocolVersion);
        QVERIFY(protocolVersionAcceptable(5));
        QVERIFY(protocolVersionAcceptable(4));
        QVERIFY(!protocolVersionAcceptable(6));
        QVERIFY(!protocolVersionAcceptable(0));
    }

    void fiveLedWholeDeviceUpdate()
    {
        std::array<Rgb, kLedCount> colors{};
        colors[0] = Rgb{0x11, 0x22, 0x33};
        colors[1] = Rgb{0x44, 0x55, 0x66};
        colors[2] = Rgb{0x77, 0x88, 0x99};
        colors[3] = Rgb{0xaa, 0xbb, 0xcc};
        colors[4] = Rgb{0xde, 0xad, 0xbe};
        const QByteArray packet = encodeUpdateLeds(7, colors);
        DecodeError error;
        const auto header = decodeHeader(packet, &error);
        QVERIFY2(header.has_value(), qPrintable(error.reason));
        QCOMPARE(header->deviceIndex, quint32(7));
        QCOMPARE(header->packetId, PacketId::UpdateLeds);
        QCOMPARE(header->payloadSize, quint32(26));
        const auto decoded = decodeUpdateLedsPayload(packet.mid(kHeaderSize), &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.reason));
        QCOMPARE(decoded->at(0).r, quint8(0x11));
        QCOMPARE(decoded->at(0).g, quint8(0x22));
        QCOMPARE(decoded->at(0).b, quint8(0x33));
        QCOMPARE(decoded->at(4).r, quint8(0xde));
        QCOMPARE(decoded->at(4).b, quint8(0xbe));
        QCOMPARE(rgbToOpenRgb(colors[0]), quint32(0x00332211));
    }

    void updateModeSerializesWaveAndDirect()
    {
        ControllerMode wave;
        wave.name = QStringLiteral("Wave");
        wave.value = 3;
        wave.flags = 0x0b;
        wave.speedMin = 1000;
        wave.speedMax = 20000;
        wave.speed = 8000;
        wave.direction = 0;
        wave.colorMode = 0;

        ControllerMode direct;
        direct.name = QStringLiteral("Direct");
        direct.value = static_cast<qint32>(0xffff);
        direct.flags = 1u << 5;
        direct.colorMode = 1;

        const QByteArray wavePacket = encodeUpdateMode(2, 3, wave, kProtocolVersion);
        DecodeError error;
        const auto header = decodeHeader(wavePacket, &error);
        QVERIFY2(header.has_value(), qPrintable(error.reason));
        QCOMPARE(header->deviceIndex, quint32(2));
        QCOMPARE(header->packetId, PacketId::UpdateMode);

        int offset = 0;
        const QByteArrayView payload(wavePacket.constData() + kHeaderSize, wavePacket.size() - kHeaderSize);
        quint32 dataSize = 0;
        quint32 modeIndex = 0;
        QVERIFY(payload.size() >= 8);
        std::memcpy(&dataSize, payload.constData(), 4);
        dataSize = qFromLittleEndian(dataSize);
        std::memcpy(&modeIndex, payload.constData() + 4, 4);
        modeIndex = qFromLittleEndian(modeIndex);
        QCOMPARE(modeIndex, quint32(3));
        QCOMPARE(dataSize, quint32(payload.size()));
        offset = 8;
        const auto decodedWave = decodeModeData(payload, offset, kProtocolVersion, &error);
        QVERIFY2(decodedWave.has_value(), qPrintable(error.reason));
        QCOMPARE(decodedWave->name, QStringLiteral("Wave"));
        QCOMPARE(decodedWave->value, qint32(3));

        const QByteArray directPacket = encodeUpdateMode(2, 0, direct, kProtocolVersion);
        const auto directHeader = decodeHeader(directPacket, &error);
        QVERIFY(directHeader.has_value());
        QCOMPARE(directHeader->packetId, PacketId::UpdateMode);
        offset = 8;
        const QByteArrayView directPayload(directPacket.constData() + kHeaderSize, directPacket.size() - kHeaderSize);
        const auto decodedDirect = decodeModeData(directPayload, offset, kProtocolVersion, &error);
        QVERIFY2(decodedDirect.has_value(), qPrintable(error.reason));
        QCOMPARE(decodedDirect->name, QStringLiteral("Direct"));
    }

    void modeNameMapping()
    {
        QCOMPARE(openRgbModeName(LightingMode::Wave), QStringLiteral("Wave"));
        QCOMPARE(openRgbModeName(LightingMode::Direct), QStringLiteral("Direct"));
        QVERIFY(openRgbModeName(LightingMode::Untouched).isEmpty());
        QCOMPARE(lightingModeFromOpenRgbName(QStringLiteral("wave")), LightingMode::Wave);
        QCOMPARE(lightingModeFromOpenRgbName(QStringLiteral("Off")), LightingMode::Off);
        QVERIFY(!lightingModeFromOpenRgbName(QStringLiteral("Rainbow")).has_value());
    }

    void untouchedDesiredStateProducesNoFrame()
    {
        QVector<ControllerMode> modes;
        ControllerMode wave;
        wave.name = QStringLiteral("Wave");
        modes.push_back(wave);

        DesiredLighting untouched;
        DecodeError error;
        const auto frames = encodeDesiredStateFrames(0, untouched, modes, kProtocolVersion, &error);
        QVERIFY(frames.has_value());
        QVERIFY(frames->isEmpty());
        QVERIFY(error.reason.isEmpty());
    }

    void desiredDirectEmitsUpdateModeThenUpdateLeds()
    {
        QVector<ControllerMode> modes(5);
        modes[0].name = QStringLiteral("Direct");
        modes[1].name = QStringLiteral("Off");
        modes[2].name = QStringLiteral("Cycle");
        modes[3].name = QStringLiteral("Wave");
        modes[4].name = QStringLiteral("Breathing");

        DesiredLighting desired;
        desired.mode = LightingMode::Direct;
        desired.colors.fill(Rgb{0x10, 0x20, 0x30});
        DecodeError error;
        const auto frames = encodeDesiredStateFrames(1, desired, modes, kProtocolVersion, &error);
        QVERIFY2(frames.has_value(), qPrintable(error.reason));
        QCOMPARE(frames->size(), 2);
        const auto modeHeader = decodeHeader(frames->at(0), &error);
        QVERIFY(modeHeader.has_value());
        QCOMPARE(modeHeader->packetId, PacketId::UpdateMode);
        const auto ledHeader = decodeHeader(frames->at(1), &error);
        QVERIFY(ledHeader.has_value());
        QCOMPARE(ledHeader->packetId, PacketId::UpdateLeds);
        QCOMPARE(ledHeader->deviceIndex, quint32(1));
    }

    void desiredStateNeverSelectsCustomMode()
    {
        QVector<ControllerMode> modes(5);
        modes[0].name = QStringLiteral("Direct");
        modes[1].name = QStringLiteral("Off");
        modes[2].name = QStringLiteral("Cycle");
        modes[3].name = QStringLiteral("Wave");
        modes[4].name = QStringLiteral("Breathing");

        DesiredLighting wave;
        wave.mode = LightingMode::Wave;
        DecodeError error;
        const auto frames = encodeDesiredStateFrames(0, wave, modes, kProtocolVersion, &error);
        QVERIFY2(frames.has_value(), qPrintable(error.reason));
        QCOMPARE(frames->size(), 1);
        const auto header = decodeHeader(frames->at(0), &error);
        QVERIFY(header.has_value());
        QCOMPARE(header->packetId, PacketId::UpdateMode);
        QVERIFY(header->packetId != PacketId::SetCustomMode);
    }

    void desiredBreathingEncodesModeSpecificColorAndSpeed()
    {
        QVector<ControllerMode> modes = g213Modes();
        modes[4].colors.clear();

        DesiredLighting desired;
        desired.mode = LightingMode::Breathing;
        desired.baseColor = Rgb{0x7c, 0x3a, 0xed};
        desired.speed = 80;

        DecodeError error;
        const auto frames = encodeDesiredStateFrames(0, desired, modes, kProtocolVersion, &error);
        QVERIFY2(frames.has_value(), qPrintable(error.reason));
        QCOMPARE(frames->size(), 1);
        const auto decoded = decodeUpdateMode(frames->at(0), &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.reason));
        QCOMPARE(decoded->name, QStringLiteral("Breathing"));
        QCOMPARE(decoded->colors.size(), 1);
        QCOMPARE(decoded->colors.at(0).r, quint8(0x7c));
        QCOMPARE(decoded->colors.at(0).g, quint8(0x3a));
        QCOMPARE(decoded->colors.at(0).b, quint8(0xed));
        QCOMPARE(decoded->speed, quint32(80));
    }

    void desiredBreathingDefaultsColorWhenUnset()
    {
        QVector<ControllerMode> modes = g213Modes();
        modes[4].colors.push_back(Rgb{0, 0, 0});

        DesiredLighting desired;
        desired.mode = LightingMode::Breathing;

        DecodeError error;
        const auto frames = encodeDesiredStateFrames(0, desired, modes, kProtocolVersion, &error);
        QVERIFY2(frames.has_value(), qPrintable(error.reason));
        const auto decoded = decodeUpdateMode(frames->at(0), &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.reason));
        QCOMPARE(decoded->colors.size(), 1);
        QCOMPARE(decoded->colors.at(0).r, quint8(0x7c));
        QCOMPARE(decoded->colors.at(0).g, quint8(0x3a));
        QCOMPARE(decoded->colors.at(0).b, quint8(0xed));
        QCOMPARE(decoded->speed, modes[4].speed);
    }

    void desiredWaveAndCycleEncodeSpeed()
    {
        QVector<ControllerMode> modes = g213Modes();

        DesiredLighting wave;
        wave.mode = LightingMode::Wave;
        wave.speed = 120;
        DecodeError error;
        const auto waveFrames = encodeDesiredStateFrames(0, wave, modes, kProtocolVersion, &error);
        QVERIFY2(waveFrames.has_value(), qPrintable(error.reason));
        const auto decodedWave = decodeUpdateMode(waveFrames->at(0), &error);
        QVERIFY2(decodedWave.has_value(), qPrintable(error.reason));
        QCOMPARE(decodedWave->name, QStringLiteral("Wave"));
        QCOMPARE(decodedWave->speed, quint32(120));
        QCOMPARE(decodedWave->colors.size(), 0);

        DesiredLighting cycle;
        cycle.mode = LightingMode::Cycle;
        cycle.speed = 40;
        const auto cycleFrames = encodeDesiredStateFrames(1, cycle, modes, kProtocolVersion, &error);
        QVERIFY2(cycleFrames.has_value(), qPrintable(error.reason));
        const auto decodedCycle = decodeUpdateMode(cycleFrames->at(0), &error);
        QVERIFY2(decodedCycle.has_value(), qPrintable(error.reason));
        QCOMPARE(decodedCycle->name, QStringLiteral("Cycle"));
        QCOMPARE(decodedCycle->speed, quint32(40));
    }

    void desiredAnimatedSpeedIsClampedToModeRange()
    {
        QVector<ControllerMode> modes = g213Modes();

        DesiredLighting tooFast;
        tooFast.mode = LightingMode::Breathing;
        tooFast.speed = 5;
        DecodeError error;
        const auto fastFrames = encodeDesiredStateFrames(0, tooFast, modes, kProtocolVersion, &error);
        QVERIFY2(fastFrames.has_value(), qPrintable(error.reason));
        QCOMPARE(decodeUpdateMode(fastFrames->at(0), &error)->speed, quint32(10));

        DesiredLighting tooSlow;
        tooSlow.mode = LightingMode::Wave;
        tooSlow.speed = 500;
        const auto slowFrames = encodeDesiredStateFrames(0, tooSlow, modes, kProtocolVersion, &error);
        QVERIFY2(slowFrames.has_value(), qPrintable(error.reason));
        QCOMPARE(decodeUpdateMode(slowFrames->at(0), &error)->speed, quint32(200));
    }

private:
    static QVector<ControllerMode> g213Modes()
    {
        QVector<ControllerMode> modes(5);
        modes[0].name = QStringLiteral("Direct");
        modes[1].name = QStringLiteral("Off");
        modes[2].name = QStringLiteral("Cycle");
        modes[2].speedMin = 200;
        modes[2].speedMax = 10;
        modes[2].speed = 50;
        modes[3].name = QStringLiteral("Wave");
        modes[3].speedMin = 200;
        modes[3].speedMax = 10;
        modes[3].speed = 50;
        modes[4].name = QStringLiteral("Breathing");
        modes[4].speedMin = 200;
        modes[4].speedMax = 10;
        modes[4].speed = 50;
        modes[4].colorsMin = 1;
        modes[4].colorsMax = 1;
        return modes;
    }

    static std::optional<ControllerMode> decodeUpdateMode(const QByteArray &packet, DecodeError *error)
    {
        const auto header = decodeHeader(packet, error);
        if (!header || header->packetId != PacketId::UpdateMode) {
            if (error && error->reason.isEmpty()) {
                error->reason = QStringLiteral("not an UPDATEMODE frame");
            }
            return std::nullopt;
        }
        const QByteArrayView payload(packet.constData() + kHeaderSize, packet.size() - kHeaderSize);
        int offset = 8;
        return decodeModeData(payload, offset, kProtocolVersion, error);
    }
};

QTEST_MAIN(TestOpenRgbProtocol)
#include "test_openrgb_protocol.moc"
