#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "broker/SessionIpc.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <systemd/sd-login.h>
#include <time.h>
#include <unistd.h>

namespace contextdeck::broker {
namespace {

constexpr int kMaxIpcClients = 4;

bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool writeAll(int fd, const uint8_t *data, std::size_t size)
{
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

const char *leaseKindFor(int fd, int leaseFd)
{
    if (leaseFd < 0) {
        return "none";
    }
    return fd == leaseFd ? "self" : "other";
}

} // namespace

bool KernelPeerCredentials::read(int fd, PeerCredentials &out) const
{
    ucred cred{};
    socklen_t len = sizeof(cred);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        return false;
    }
    if (len != sizeof(cred) || cred.pid <= 0) {
        return false;
    }
    out.pid = cred.pid;
    out.uid = cred.uid;
    out.gid = cred.gid;
    return true;
}

FixedUidAuthorizer::FixedUidAuthorizer(uid_t uid)
    : uid_(uid)
{
}

bool FixedUidAuthorizer::authorize(const PeerCredentials &cred) const
{
    if (cred.pid <= 0 || cred.uid == 0) {
        return false;
    }
    return cred.uid == uid_;
}

bool LogindSeatAuthorizer::authorize(const PeerCredentials &cred) const
{
    if (cred.pid <= 0 || cred.uid == 0) {
        return false;
    }

    char *session = nullptr;
    if (sd_pid_get_session(cred.pid, &session) < 0 || session == nullptr) {
        return false;
    }

    uid_t sessionUid = static_cast<uid_t>(-1);
    const int uidRc = sd_session_get_uid(session, &sessionUid);
    char *seat = nullptr;
    const int seatRc = sd_session_get_seat(session, &seat);
    char *type = nullptr;
    const int typeRc = sd_session_get_type(session, &type);
    const int active = sd_session_is_active(session);
    const int remote = sd_session_is_remote(session);

    const bool uidMatch = uidRc >= 0 && sessionUid == cred.uid;
    const bool seated = seatRc >= 0 && seat != nullptr && seat[0] != '\0';
    const bool graphical = typeRc >= 0 && type != nullptr
        && (std::strcmp(type, "wayland") == 0 || std::strcmp(type, "x11") == 0);
    const bool ok = uidMatch && seated && graphical && active > 0 && remote == 0;

    free(type);
    free(seat);
    free(session);
    return ok;
}

SessionIpc::SessionIpc(SignalEpollWait &wait, IArmControl &arm, ISessionAuthorizer &auth, IPeerCredentials &creds,
                       Logger &logger, const uint64_t *nowMs)
    : wait_(wait)
    , arm_(arm)
    , auth_(auth)
    , creds_(creds)
    , logger_(logger)
    , nowOverride_(nowMs)
{
}

SessionIpc::~SessionIpc()
{
    shutdown();
}

uint64_t SessionIpc::nowMs() const
{
    if (nowOverride_ != nullptr) {
        return *nowOverride_;
    }
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec / 1000000u);
}

uint32_t SessionIpc::remainingTtl() const
{
    if (leaseFd_ < 0) {
        return 0;
    }
    const uint64_t now = nowMs();
    if (now >= leaseExpiryMs_) {
        return 0;
    }
    const uint64_t left = leaseExpiryMs_ - now;
    return left > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(left);
}

SessionIpc::Client *SessionIpc::findClient(int fd)
{
    for (Client &client : clients_) {
        if (client.fd == fd) {
            return &client;
        }
    }
    return nullptr;
}

bool SessionIpc::authenticateFd(int fd)
{
    PeerCredentials cred;
    if (!creds_.read(fd, cred)) {
        logger_.error("peer-cred-failed");
        return false;
    }
    if (!auth_.authorize(cred)) {
        logger_.error("peer-rejected");
        return false;
    }
    return true;
}

bool SessionIpc::addClient(int fd)
{
    if (!setNonBlocking(fd) || !wait_.addFd(fd)) {
        return false;
    }
    Client client;
    client.fd = fd;
    clients_.push_back(std::move(client));
    return true;
}

void SessionIpc::releaseLease(const char *reason)
{
    if (leaseFd_ < 0 && !arm_.armed()) {
        return;
    }
    arm_.disarm();
    leaseFd_ = -1;
    leaseExpiryMs_ = 0;
    logger_.state(reason);
}

void SessionIpc::dropClient(int fd, const char *reason)
{
    if (findClient(fd) == nullptr) {
        return;
    }
    if (fd == leaseFd_) {
        releaseLease(reason);
    }
    wait_.removeFd(fd);
    ::close(fd);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it->fd == fd) {
            clients_.erase(it);
            break;
        }
    }
}

bool SessionIpc::writePayload(int fd, std::string_view payload)
{
    std::vector<uint8_t> frame;
    if (!encodeIpcFrame(payload, frame)) {
        return false;
    }
    if (!writeAll(fd, frame.data(), frame.size())) {
        dropClient(fd, "lease-disconnect");
        return false;
    }
    return true;
}

bool SessionIpc::listen(const char *path)
{
    if (path == nullptr || path[0] == '\0' || listenFd_ >= 0) {
        return false;
    }
    if (std::strlen(path) >= sizeof(sockaddr_un::sun_path)) {
        logger_.error("ipc-path-too-long");
        return false;
    }

    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listenFd_ < 0) {
        logger_.error("ipc-socket-failed");
        return false;
    }

    ::unlink(path);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        logger_.error("ipc-bind-failed");
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::chmod(path, 0666) != 0) {
        logger_.error("ipc-chmod-failed");
        ::unlink(path);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::listen(listenFd_, kMaxIpcClients) != 0 || !wait_.addFd(listenFd_)) {
        logger_.error("ipc-listen-failed");
        ::unlink(path);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    path_ = path;
    logger_.state("ipc-listening");
    return true;
}

bool SessionIpc::attach(int fd)
{
    if (fd < 0 || findClient(fd) != nullptr) {
        return false;
    }
    if (static_cast<int>(clients_.size()) >= kMaxIpcClients) {
        logger_.error("ipc-client-limit");
        return false;
    }
    if (!authenticateFd(fd)) {
        return false;
    }
    if (!addClient(fd)) {
        logger_.error("ipc-attach-failed");
        return false;
    }
    return true;
}

void SessionIpc::acceptOne()
{
    const int fd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) {
        return;
    }
    if (static_cast<int>(clients_.size()) >= kMaxIpcClients) {
        logger_.error("ipc-client-limit");
        ::close(fd);
        return;
    }
    if (!authenticateFd(fd)) {
        ::close(fd);
        return;
    }
    if (!addClient(fd)) {
        logger_.error("ipc-accept-failed");
        ::close(fd);
        return;
    }
    handleClient(fd);
}

void SessionIpc::dispatch(Client &client, const IpcRequest &request)
{
    if (!request.wellFormed) {
        const int fd = client.fd;
        (void)writePayload(fd, "ERR MALFORMED");
        dropClient(fd, "ipc-malformed");
        return;
    }

    const bool holder = client.fd == leaseFd_;
    switch (request.verb) {
    case IpcVerb::Status:
        writePayload(client.fd, ipcOkStatus(leaseKindFor(client.fd, leaseFd_), arm_.armed() ? 1 : 0, remainingTtl()));
        return;
    case IpcVerb::Lease: {
        const uint32_t ttl = request.leaseMs.value_or(holder ? leaseTtlMs_ : kDefaultLeaseMs);
        if (leaseFd_ >= 0 && !holder) {
            writePayload(client.fd, "ERR LEASE_HELD");
            return;
        }
        leaseFd_ = client.fd;
        leaseTtlMs_ = ttl;
        leaseExpiryMs_ = nowMs() + ttl;
        logger_.state(holder ? "lease-renewed" : "lease-acquired");
        writePayload(client.fd, ipcOkLease(ttl));
        return;
    }
    case IpcVerb::Heartbeat: {
        if (!holder) {
            writePayload(client.fd, leaseFd_ >= 0 ? "ERR UNAUTHORIZED" : "ERR NO_LEASE");
            return;
        }
        const uint32_t ttl = request.leaseMs.value_or(leaseTtlMs_);
        leaseTtlMs_ = ttl;
        leaseExpiryMs_ = nowMs() + ttl;
        logger_.state("lease-renewed");
        writePayload(client.fd, ipcOkLease(ttl));
        return;
    }
    case IpcVerb::Arm: {
        if (!holder) {
            writePayload(client.fd, leaseFd_ >= 0 ? "ERR UNAUTHORIZED" : "ERR NO_LEASE");
            return;
        }
        if (request.leaseMs.has_value()) {
            leaseTtlMs_ = *request.leaseMs;
            leaseExpiryMs_ = nowMs() + leaseTtlMs_;
        }
        if (!arm_.arm()) {
            writePayload(client.fd, "ERR ARM_FAILED");
            return;
        }
        writePayload(client.fd, "OK ARMED");
        return;
    }
    case IpcVerb::Disarm: {
        if (!holder) {
            writePayload(client.fd, leaseFd_ >= 0 ? "ERR UNAUTHORIZED" : "ERR NO_LEASE");
            return;
        }
        arm_.disarm();
        writePayload(client.fd, "OK DISARMED");
        return;
    }
    case IpcVerb::Release: {
        if (!holder) {
            writePayload(client.fd, leaseFd_ >= 0 ? "ERR UNAUTHORIZED" : "ERR NO_LEASE");
            return;
        }
        releaseLease("lease-released");
        writePayload(client.fd, "OK RELEASED");
        return;
    }
    case IpcVerb::Unknown:
        writePayload(client.fd, "ERR UNKNOWN");
        return;
    }
}

void SessionIpc::handleClient(int fd)
{
    Client *client = findClient(fd);
    if (client == nullptr) {
        return;
    }

    for (;;) {
        uint8_t tmp[256];
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            client->inbuf.insert(client->inbuf.end(), tmp, tmp + n);
            if (client->inbuf.size() > (kIpcMaxPayload * 2) + 4) {
                dropClient(fd, "ipc-malformed");
                return;
            }
            continue;
        }
        if (n == 0) {
            dropClient(fd, "lease-disconnect");
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        dropClient(fd, "lease-disconnect");
        return;
    }

    while (!client->inbuf.empty()) {
        std::string payload;
        const FrameStatus status = decodeIpcFrame(client->inbuf, payload);
        if (status == FrameStatus::NeedMore) {
            return;
        }
        if (status == FrameStatus::Malformed) {
            const int clientFd = fd;
            (void)writePayload(clientFd, "ERR MALFORMED");
            dropClient(clientFd, "ipc-malformed");
            return;
        }
        dispatch(*client, parseIpcPayload(payload));
        client = findClient(fd);
        if (client == nullptr) {
            return;
        }
    }
}

void SessionIpc::onFd(int fd)
{
    if (fd == listenFd_) {
        acceptOne();
        return;
    }
    handleClient(fd);
}

void SessionIpc::expireLease()
{
    if (leaseFd_ < 0) {
        return;
    }
    if (nowMs() < leaseExpiryMs_) {
        return;
    }
    const int fd = leaseFd_;
    dropClient(fd, "lease-expired");
}

void SessionIpc::afterWait()
{
    expireLease();
    const std::vector<int> ready = wait_.readyFds();
    for (int fd : ready) {
        onFd(fd);
    }
}

void SessionIpc::shutdown()
{
    releaseLease("ipc-stopped");
    while (!clients_.empty()) {
        const int fd = clients_.front().fd;
        wait_.removeFd(fd);
        ::close(fd);
        clients_.erase(clients_.begin());
    }
    if (listenFd_ >= 0) {
        wait_.removeFd(listenFd_);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
        path_.clear();
    }
}

} // namespace contextdeck::broker
