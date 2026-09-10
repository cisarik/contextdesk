#include "rgb/OpenRgbProtocol.h"

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
};

QTEST_MAIN(TestOpenRgbProtocol)
#include "test_openrgb_protocol.moc"
