#include "app/BrokerIpcClient.h"

#include "broker/IpcProtocol.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTest>

#include <vector>

using contextdeck::broker::decodeIpcFrame;
using contextdeck::broker::encodeIpcFrame;
using contextdeck::broker::FrameStatus;

namespace {

class MockBrokerServer : public QObject
{
public:
    QStringList received;
    QStringList receivedAfterReset;
    bool capturingPostReset = false;

    explicit MockBrokerServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&server, &QLocalServer::newConnection, this, [this]() { onNewConnection(); });
    }

    ~MockBrokerServer() override
    {
        closeClient();
        server.close();
    }

    bool listen(const QString &name)
    {
        QLocalServer::removeServer(name);
        return server.listen(name);
    }

    void closeClient()
    {
        QLocalSocket *sock = client.data();
        client.clear();
        buffer.clear();
        if (sock == nullptr) {
            return;
        }
        sock->disconnect(this);
        sock->abort();
    }

    void resetCapture()
    {
        capturingPostReset = true;
        receivedAfterReset.clear();
    }

private:
    void onNewConnection()
    {
        closeClient();
        client = server.nextPendingConnection();
        if (client == nullptr) {
            return;
        }
        buffer.clear();
        connect(client, &QLocalSocket::readyRead, this, [this]() { onReady(); });
        connect(client, &QLocalSocket::disconnected, this, [this]() {
            buffer.clear();
            client = nullptr;
        });
    }

    void onReady()
    {
        if (client == nullptr) {
            return;
        }
        const QByteArray chunk = client->readAll();
        buffer.insert(buffer.end(), chunk.cbegin(), chunk.cend());
        for (;;) {
            std::string payload;
            const FrameStatus status = decodeIpcFrame(buffer, payload);
            if (status == FrameStatus::NeedMore) {
                return;
            }
            if (status == FrameStatus::Malformed) {
                client->abort();
                return;
            }
            const QString text = QString::fromStdString(payload);
            received.append(text);
            if (capturingPostReset) {
                receivedAfterReset.append(text);
            }
            QByteArray reply;
            if (text.startsWith(QLatin1String("STATUS"))) {
                reply = QByteArrayLiteral("OK STATUS lease=none armed=0 ttl=6000");
            } else if (text.startsWith(QLatin1String("LEASE"))) {
                reply = QByteArrayLiteral("OK LEASE ttl=6000");
            } else if (text.startsWith(QLatin1String("ARM"))) {
                reply = QByteArrayLiteral("OK ARMED");
            } else if (text.startsWith(QLatin1String("DISARM"))) {
                reply = QByteArrayLiteral("OK DISARMED");
            } else if (text.startsWith(QLatin1String("RELEASE"))) {
                reply = QByteArrayLiteral("OK RELEASED");
            } else if (text.startsWith(QLatin1String("HEARTBEAT"))) {
                reply = QByteArrayLiteral("OK HEARTBEAT");
            } else {
                reply = QByteArrayLiteral("ERR UNKNOWN");
            }
            std::vector<uint8_t> frame;
            if (!encodeIpcFrame(std::string_view(reply.constData(), static_cast<std::size_t>(reply.size())), frame)) {
                return;
            }
            client->write(reinterpret_cast<const char *>(frame.data()), static_cast<qint64>(frame.size()));
            client->flush();
        }
    }

    QLocalServer server;
    QPointer<QLocalSocket> client;
    std::vector<uint8_t> buffer;
};

bool containsVerb(const QStringList &payloads, const QString &verb)
{
    for (const QString &payload : payloads) {
        if (payload == verb || payload.startsWith(verb + QLatin1Char(' '))) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestBrokerIpcClient : public QObject
{
    Q_OBJECT

private slots:
    void startAndReconnectNeverAutoArm()
    {
        const QString name = QStringLiteral("contextdeck-ipc-client-test-%1").arg(QCoreApplication::applicationPid());
        qputenv("CONTEXTDECK_BROKER_SOCKET", QFile::encodeName(name));

        MockBrokerServer server;
        QVERIFY(server.listen(name));

        {
            contextdeck::BrokerIpcClient client;
            client.start();
            QTRY_VERIFY(client.connected());
            QTRY_VERIFY(containsVerb(server.received, QStringLiteral("STATUS")));
            QVERIFY(!containsVerb(server.received, QStringLiteral("LEASE")));
            QVERIFY(!containsVerb(server.received, QStringLiteral("ARM")));
            QCOMPARE(client.stateText(), QStringLiteral("OK STATUS lease=none armed=0 ttl=6000"));

            client.arm();
            QTRY_VERIFY(containsVerb(server.received, QStringLiteral("LEASE")));
            QTRY_VERIFY(containsVerb(server.received, QStringLiteral("ARM")));
            QTRY_COMPARE(client.stateText(), QStringLiteral("armed"));

            server.closeClient();
            QTRY_VERIFY(!client.connected());
            QCOMPARE(client.stateText(), QStringLiteral("disconnected"));

            server.resetCapture();
            client.start();
            QTRY_VERIFY(client.connected());
            QTRY_VERIFY(containsVerb(server.receivedAfterReset, QStringLiteral("STATUS")));
            QTest::qWait(80);
            QVERIFY(!containsVerb(server.receivedAfterReset, QStringLiteral("LEASE")));
            QVERIFY(!containsVerb(server.receivedAfterReset, QStringLiteral("ARM")));
            QVERIFY(client.stateText().startsWith(QStringLiteral("OK STATUS")));
            QVERIFY(!client.stateText().contains(QStringLiteral("armed=1")));
            server.closeClient();
            QTRY_VERIFY(!client.connected());
        }
        QCoreApplication::processEvents();
    }

    void statusProbeDoesNotArm()
    {
        const QString name = QStringLiteral("contextdeck-ipc-client-status-%1").arg(QCoreApplication::applicationPid());
        qputenv("CONTEXTDECK_BROKER_SOCKET", QFile::encodeName(name));

        MockBrokerServer server;
        QVERIFY(server.listen(name));

        {
            contextdeck::BrokerIpcClient client;
            client.start();
            QTRY_VERIFY(client.connected());
            client.requestStatus();
            QTRY_VERIFY(server.received.size() >= 2);
            QTest::qWait(50);
            QVERIFY(!containsVerb(server.received, QStringLiteral("LEASE")));
            QVERIFY(!containsVerb(server.received, QStringLiteral("ARM")));
            server.closeClient();
            QTRY_VERIFY(!client.connected());
        }
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(TestBrokerIpcClient)
#include "test_broker_ipc_client.moc"
