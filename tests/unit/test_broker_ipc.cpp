#include "broker/Acquisition.h"
#include "broker/EventLoop.h"
#include "broker/FakeGrabber.h"
#include "broker/GrabbingSource.h"
#include "broker/IdleWait.h"
#include "broker/IpcProtocol.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"
#include "broker/SessionIpc.h"
#include "broker/Watchdog.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

using contextdeck::broker::Acquisition;
using contextdeck::broker::AcquisitionArmControl;
using contextdeck::broker::EventLoop;
using contextdeck::broker::FakeGrabber;
using contextdeck::broker::FixedUidAuthorizer;
using contextdeck::broker::FrameStatus;
using contextdeck::broker::GrabbingSource;
using contextdeck::broker::IArmControl;
using contextdeck::broker::ILifecycleSink;
using contextdeck::broker::IPeerCredentials;
using contextdeck::broker::IpcRequest;
using contextdeck::broker::IpcVerb;
using contextdeck::broker::KernelPeerCredentials;
using contextdeck::broker::KeyLedger;
using contextdeck::broker::LogindSeatAuthorizer;
using contextdeck::broker::Logger;
using contextdeck::broker::PeerCredentials;
using contextdeck::broker::SessionIpc;
using contextdeck::broker::SignalEpollWait;
using contextdeck::broker::SourceTag;
using contextdeck::broker::decodeIpcFrame;
using contextdeck::broker::encodeIpcFrame;
using contextdeck::broker::kDefaultLeaseMs;
using contextdeck::broker::parseIpcPayload;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_ipc.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

class FakeArm final : public IArmControl {
public:
    bool allow = true;
    int arms = 0;
    int disarms = 0;
    bool armed_ = false;

    bool arm() override
    {
        ++arms;
        if (!allow) {
            return false;
        }
        armed_ = true;
        return true;
    }

    void disarm() override
    {
        if (!armed_) {
            return;
        }
        ++disarms;
        armed_ = false;
    }

    bool armed() const override { return armed_; }
};

class ScriptedCreds final : public IPeerCredentials {
public:
    PeerCredentials cred;
    bool ok = true;

    bool read(int, PeerCredentials &out) const override
    {
        out = cred;
        return ok;
    }
};

class RecordingSink final : public ILifecycleSink {
public:
    bool createVirtual() override
    {
        created = true;
        destroyed = false;
        return true;
    }
    void destroyVirtual() override { destroyed = true; }

    bool created = false;
    bool destroyed = false;
};

class RecordingWatchdog final : public contextdeck::broker::IWatchdog {
public:
    int ready = 0;
    int watchdog = 0;
    void notifyReady() override { ++ready; }
    void notifyWatchdog() override { ++watchdog; }
};

bool sendPayload(int fd, std::string_view payload)
{
    std::vector<uint8_t> frame;
    if (!encodeIpcFrame(payload, frame)) {
        return false;
    }
    return ::send(fd, frame.data(), frame.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(frame.size());
}

std::string recvPayload(int fd, int timeoutMs = 200)
{
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, timeoutMs) <= 0) {
        return {};
    }
    uint8_t buf[512];
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n < 2) {
        return {};
    }
    std::vector<uint8_t> bytes(buf, buf + n);
    std::string payload;
    if (decodeIpcFrame(bytes, payload) != FrameStatus::Ok) {
        return {};
    }
    return payload;
}

std::string roundTrip(SessionIpc &ipc, int serverFd, int clientFd, std::string_view payload)
{
    if (!sendPayload(clientFd, payload)) {
        return {};
    }
    ipc.onFd(serverFd);
    return recvPayload(clientFd);
}

bool makePair(int &serverFd, int &clientFd)
{
    int sv[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
        return false;
    }
    serverFd = sv[0];
    clientFd = sv[1];
    return true;
}

PeerCredentials selfCred()
{
    PeerCredentials cred;
    cred.pid = ::getpid();
    cred.uid = ::getuid();
    cred.gid = ::getgid();
    return cred;
}

void testProtocol()
{
    std::vector<uint8_t> frame;
    EXPECT(encodeIpcFrame("STATUS", frame));
    EXPECT(frame.size() == 8);
    std::string payload;
    EXPECT(decodeIpcFrame(frame, payload) == FrameStatus::Ok);
    EXPECT(payload == "STATUS");
    EXPECT(frame.empty());

    const IpcRequest status = parseIpcPayload("STATUS");
    EXPECT(status.wellFormed && status.verb == IpcVerb::Status);

    const IpcRequest lease = parseIpcPayload("LEASE 6000");
    EXPECT(lease.wellFormed && lease.verb == IpcVerb::Lease && lease.leaseMs == kDefaultLeaseMs);

    const IpcRequest unknown = parseIpcPayload("INJECT");
    EXPECT(unknown.wellFormed && unknown.verb == IpcVerb::Unknown);

    const IpcRequest forged = parseIpcPayload("LEASE uid=1000");
    EXPECT(!forged.wellFormed);

    std::vector<uint8_t> emptyLen{0, 0};
    EXPECT(decodeIpcFrame(emptyLen, payload) == FrameStatus::Malformed);

    std::vector<uint8_t> tooBig{1, 1}; // length 257
    tooBig[0] = 1;
    tooBig[1] = 1;
    EXPECT(decodeIpcFrame(tooBig, payload) == FrameStatus::Malformed);
}

void testPeercredAcceptReject()
{
    SignalEpollWait wait;
    EXPECT(wait.valid());
    FakeArm arm;
    Logger logger;
    KernelPeerCredentials kernel;
    FixedUidAuthorizer allow(::getuid());
    SessionIpc ipc(wait, arm, allow, kernel, logger);

    int serverFd = -1;
    int clientFd = -1;
    EXPECT(makePair(serverFd, clientFd));
    EXPECT(ipc.attach(serverFd));
    EXPECT(roundTrip(ipc, serverFd, clientFd, "STATUS").find("OK STATUS lease=none armed=0") == 0);
    EXPECT(!arm.armed());
    ipc.shutdown();
    ::close(clientFd);

    FakeArm arm2;
    FixedUidAuthorizer deny(::getuid() == 1 ? 2 : 1);
    SessionIpc ipc2(wait, arm2, deny, kernel, logger);
    EXPECT(makePair(serverFd, clientFd));
    EXPECT(!ipc2.attach(serverFd));
    EXPECT(!arm2.armed());
    EXPECT(arm2.arms == 0);
    sendPayload(clientFd, "ARM");
    EXPECT(recvPayload(clientFd, 50).empty());
    ::close(serverFd);
    ::close(clientFd);

    ScriptedCreds failCreds;
    failCreds.ok = false;
    FakeArm arm3;
    SessionIpc ipc3(wait, arm3, allow, failCreds, logger);
    EXPECT(makePair(serverFd, clientFd));
    EXPECT(!ipc3.attach(serverFd));
    EXPECT(!arm3.armed());
    ::close(serverFd);
    ::close(clientFd);

    ScriptedCreds rootCreds;
    rootCreds.cred = selfCred();
    rootCreds.cred.uid = 0;
    FakeArm arm4;
    SessionIpc ipc4(wait, arm4, allow, rootCreds, logger);
    EXPECT(makePair(serverFd, clientFd));
    EXPECT(!ipc4.attach(serverFd));
    EXPECT(!arm4.armed());
    ::close(serverFd);
    ::close(clientFd);
}

void testLeaseArmAndDuplicate()
{
    SignalEpollWait wait;
    FakeArm arm;
    Logger logger;
    uint64_t now = 1000;
    KernelPeerCredentials kernel;
    FixedUidAuthorizer allow(::getuid());
    SessionIpc ipc(wait, arm, allow, kernel, logger, &now);

    int serverA = -1;
    int clientA = -1;
    int serverB = -1;
    int clientB = -1;
    EXPECT(makePair(serverA, clientA));
    EXPECT(makePair(serverB, clientB));
    EXPECT(ipc.attach(serverA));
    EXPECT(ipc.attach(serverB));

    EXPECT(roundTrip(ipc, serverA, clientA, "ARM") == "ERR NO_LEASE");
    EXPECT(!arm.armed());

    EXPECT(roundTrip(ipc, serverA, clientA, "LEASE").find("OK LEASE ttl=6000") == 0);
    EXPECT(ipc.leaseHeld());
    EXPECT(!arm.armed());

    EXPECT(roundTrip(ipc, serverB, clientB, "LEASE") == "ERR LEASE_HELD");
    EXPECT(roundTrip(ipc, serverB, clientB, "ARM") == "ERR UNAUTHORIZED");
    EXPECT(roundTrip(ipc, serverB, clientB, "DISARM") == "ERR UNAUTHORIZED");
    EXPECT(roundTrip(ipc, serverB, clientB, "INJECT") == "ERR UNKNOWN");
    EXPECT(!arm.armed());

    EXPECT(roundTrip(ipc, serverA, clientA, "ARM") == "OK ARMED");
    EXPECT(arm.armed());
    EXPECT(roundTrip(ipc, serverA, clientA, "DISARM") == "OK DISARMED");
    EXPECT(!arm.armed());
    EXPECT(ipc.leaseHeld());
    EXPECT(roundTrip(ipc, serverA, clientA, "ARM") == "OK ARMED");
    EXPECT(roundTrip(ipc, serverA, clientA, "HEARTBEAT").find("OK LEASE") == 0);
    EXPECT(roundTrip(ipc, serverA, clientA, "RELEASE") == "OK RELEASED");
    EXPECT(!arm.armed());
    EXPECT(!ipc.leaseHeld());

    ipc.shutdown();
    ::close(clientA);
    ::close(clientB);
}

void testDisconnectMalformedExpiryShutdown()
{
    SignalEpollWait wait;
    Logger logger;
    uint64_t now = 0;
    KernelPeerCredentials kernel;
    FixedUidAuthorizer allow(::getuid());

    std::vector<std::string> grabLog;
    FakeGrabber g0("if00", grabLog);
    FakeGrabber g1("if01", grabLog);
    GrabbingSource s0(SourceTag::If00, g0);
    GrabbingSource s1(SourceTag::If01, g1);
    RecordingSink sink;
    KeyLedger ledger;
    Acquisition acq(sink, s0, s1, ledger, logger);
    AcquisitionArmControl control(acq);
    SessionIpc ipc(wait, control, allow, kernel, logger, &now);

    int serverFd = -1;
    int clientFd = -1;
    EXPECT(makePair(serverFd, clientFd));
    EXPECT(ipc.attach(serverFd));
    EXPECT(roundTrip(ipc, serverFd, clientFd, "LEASE 1000").find("OK LEASE ttl=1000") == 0);
    EXPECT(roundTrip(ipc, serverFd, clientFd, "ARM") == "OK ARMED");
    EXPECT(acq.armed());
    EXPECT(g0.grabbed() && g1.grabbed());

    ::close(clientFd);
    clientFd = -1;
    ipc.onFd(serverFd);
    EXPECT(!acq.armed());
    EXPECT(!g0.grabbed() && !g1.grabbed());
    EXPECT(!ipc.leaseHeld());
    const auto unclaimIf01 = std::find(acq.history().begin(), acq.history().end(), "unclaim-if01");
    const auto synthetic = std::find(acq.history().begin(), acq.history().end(), "synthetic-disarm");
    const auto destroy = std::find(acq.history().begin(), acq.history().end(), "destroy-virtual");
    EXPECT(unclaimIf01 != acq.history().end());
    EXPECT(synthetic != acq.history().end());
    EXPECT(destroy != acq.history().end());
    EXPECT(unclaimIf01 < synthetic);
    EXPECT(synthetic < destroy);

    EXPECT(makePair(serverFd, clientFd));
    EXPECT(ipc.attach(serverFd));
    EXPECT(roundTrip(ipc, serverFd, clientFd, "LEASE 1000").find("OK LEASE") == 0);
    EXPECT(roundTrip(ipc, serverFd, clientFd, "ARM") == "OK ARMED");
    const uint8_t bad[] = {0, 0};
    (void)::send(clientFd, bad, sizeof(bad), MSG_NOSIGNAL);
    ipc.onFd(serverFd);
    EXPECT(!acq.armed());
    EXPECT(!ipc.leaseHeld());
    ::close(clientFd);

    EXPECT(makePair(serverFd, clientFd));
    EXPECT(ipc.attach(serverFd));
    EXPECT(roundTrip(ipc, serverFd, clientFd, "LEASE 1000").find("OK LEASE") == 0);
    EXPECT(roundTrip(ipc, serverFd, clientFd, "ARM") == "OK ARMED");
    now = 1001;
    ipc.expireLease();
    EXPECT(!acq.armed());
    EXPECT(!ipc.leaseHeld());
    ::close(clientFd);

    EXPECT(makePair(serverFd, clientFd));
    EXPECT(ipc.attach(serverFd));
    EXPECT(roundTrip(ipc, serverFd, clientFd, "LEASE").find("OK LEASE") == 0);
    EXPECT(roundTrip(ipc, serverFd, clientFd, "ARM") == "OK ARMED");
    ipc.shutdown();
    EXPECT(!acq.armed());
    EXPECT(!ipc.leaseHeld());
    ::close(clientFd);
}

void testListenUnixSocketAndLoop()
{
    SignalEpollWait wait;
    FakeArm arm;
    Logger logger;
    KernelPeerCredentials kernel;
    FixedUidAuthorizer allow(::getuid());
    SessionIpc ipc(wait, arm, allow, kernel, logger);

    char dir[] = "/tmp/contextdeck-ipc-XXXXXX";
    EXPECT(::mkdtemp(dir) != nullptr);
    std::string path = std::string(dir) + "/broker.sock";
    EXPECT(ipc.listen(path.c_str()));

    struct stat st {};
    EXPECT(::stat(path.c_str(), &st) == 0);
    EXPECT((st.st_mode & 0777) == 0666);

    const int client = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    EXPECT(client >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    EXPECT(::connect(client, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);

    RecordingWatchdog wd;
    EventLoop loop(wait, wd, 50, &ipc);
    EXPECT(sendPayload(client, "STATUS"));
    EXPECT(loop.step() == contextdeck::broker::WaitOutcome::Progress);
    const std::string reply = recvPayload(client, 200);
    EXPECT(reply.find("OK STATUS lease=none armed=0") == 0);
    EXPECT(!arm.armed());
    EXPECT(wd.ready == 1);

    EXPECT(sendPayload(client, "LEASE"));
    EXPECT(loop.step() == contextdeck::broker::WaitOutcome::Progress);
    EXPECT(recvPayload(client).find("OK LEASE") == 0);
    EXPECT(sendPayload(client, "ARM"));
    EXPECT(loop.step() == contextdeck::broker::WaitOutcome::Progress);
    EXPECT(recvPayload(client) == "OK ARMED");
    EXPECT(arm.armed());

    ::close(client);
    EXPECT(loop.step() == contextdeck::broker::WaitOutcome::Progress);
    EXPECT(!arm.armed());

    ipc.shutdown();
    ::unlink(path.c_str());
    ::rmdir(dir);
}

void testLogindRejectsRootAndBadPid()
{
    LogindSeatAuthorizer auth;
    PeerCredentials cred = selfCred();
    cred.uid = 0;
    EXPECT(!auth.authorize(cred));
    cred = selfCred();
    cred.pid = 0;
    EXPECT(!auth.authorize(cred));
}

void testNoArmBeforeAuthOnListen()
{
    SignalEpollWait wait;
    FakeArm arm;
    Logger logger;
    ScriptedCreds creds;
    creds.ok = false;
    FixedUidAuthorizer allow(::getuid());
    SessionIpc ipc(wait, arm, allow, creds, logger);

    char dir[] = "/tmp/contextdeck-ipc-XXXXXX";
    EXPECT(::mkdtemp(dir) != nullptr);
    std::string path = std::string(dir) + "/broker.sock";
    EXPECT(ipc.listen(path.c_str()));

    const int client = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    EXPECT(client >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    EXPECT(::connect(client, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);

    RecordingWatchdog wd;
    EventLoop loop(wait, wd, 50, &ipc);
    EXPECT(loop.step() == contextdeck::broker::WaitOutcome::Progress);
    EXPECT(ipc.clientCount() == 0);
    EXPECT(!arm.armed());
    sendPayload(client, "LEASE");
    sendPayload(client, "ARM");
    EXPECT(loop.step() == contextdeck::broker::WaitOutcome::Progress);
    EXPECT(!arm.armed());
    EXPECT(ipc.clientCount() == 0);

    ::close(client);
    ipc.shutdown();
    ::rmdir(dir);
}

} // namespace

int main()
{
    if (::getuid() == 0) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_ipc.cpp: must not run as root\n");
        return 1;
    }
    testProtocol();
    testPeercredAcceptReject();
    testLeaseArmAndDuplicate();
    testDisconnectMalformedExpiryShutdown();
    testListenUnixSocketAndLoop();
    testLogindRejectsRootAndBadPid();
    testNoArmBeforeAuthOnListen();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d ipc test(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
