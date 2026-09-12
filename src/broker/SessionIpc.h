#pragma once

#include "broker/Acquisition.h"
#include "broker/EventLoop.h"
#include "broker/IdleWait.h"
#include "broker/IpcProtocol.h"
#include "broker/Logger.h"

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace contextdeck::broker {

struct PeerCredentials {
    pid_t pid = 0;
    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
};

class IPeerCredentials {
public:
    virtual ~IPeerCredentials() = default;
    virtual bool read(int fd, PeerCredentials &out) const = 0;
};

class KernelPeerCredentials final : public IPeerCredentials {
public:
    bool read(int fd, PeerCredentials &out) const override;
};

class ISessionAuthorizer {
public:
    virtual ~ISessionAuthorizer() = default;
    virtual bool authorize(const PeerCredentials &cred) const = 0;
};

class FixedUidAuthorizer final : public ISessionAuthorizer {
public:
    explicit FixedUidAuthorizer(uid_t uid);
    bool authorize(const PeerCredentials &cred) const override;

private:
    uid_t uid_;
};

struct LoginSessionView {
    uid_t uid = static_cast<uid_t>(-1);
    std::string seat;
    std::string type;
    bool active = false;
    bool remote = false;
};

class ILoginLookup {
public:
    virtual ~ILoginLookup() = default;
    virtual int pidSession(pid_t pid, std::string &session) const = 0;
    virtual bool inspectSession(const std::string &session, LoginSessionView &out) const = 0;
    virtual std::vector<std::string> sessionsForUid(uid_t uid) const = 0;
};

class SystemdLoginLookup final : public ILoginLookup {
public:
    int pidSession(pid_t pid, std::string &session) const override;
    bool inspectSession(const std::string &session, LoginSessionView &out) const override;
    std::vector<std::string> sessionsForUid(uid_t uid) const override;
};

class LogindSeatAuthorizer final : public ISessionAuthorizer {
public:
    LogindSeatAuthorizer();
    explicit LogindSeatAuthorizer(const ILoginLookup &lookup);
    bool authorize(const PeerCredentials &cred) const override;

private:
    bool eligible(const LoginSessionView &view, uid_t uid) const;

    SystemdLoginLookup owned_;
    const ILoginLookup *lookup_ = nullptr;
};

class IArmControl {
public:
    virtual ~IArmControl() = default;
    virtual bool arm() = 0;
    virtual void disarm() = 0;
    virtual bool armed() const = 0;
};

class AcquisitionArmControl final : public IArmControl {
public:
    explicit AcquisitionArmControl(Acquisition &acquisition)
        : acquisition_(acquisition)
    {
    }

    bool arm() override { return acquisition_.arm(); }
    void disarm() override { acquisition_.disarm(); }
    bool armed() const override { return acquisition_.armed(); }

private:
    Acquisition &acquisition_;
};

class SessionIpc final : public ILoopWork {
public:
    SessionIpc(SignalEpollWait &wait, IArmControl &arm, ISessionAuthorizer &auth, IPeerCredentials &creds,
               Logger &logger, const uint64_t *nowMs = nullptr);
    ~SessionIpc() override;

    SessionIpc(const SessionIpc &) = delete;
    SessionIpc &operator=(const SessionIpc &) = delete;

    bool listen(const char *path);
    bool attach(int fd);
    void onFd(int fd);
    void expireLease();
    void afterWait() override;
    void shutdown();

    bool leaseHeld() const { return leaseFd_ >= 0; }
    int leaseFd() const { return leaseFd_; }
    uint32_t leaseTtlMs() const { return leaseTtlMs_; }
    int listenFd() const { return listenFd_; }
    int clientCount() const { return static_cast<int>(clients_.size()); }

private:
    struct Client {
        int fd = -1;
        std::vector<uint8_t> inbuf;
    };

    uint64_t nowMs() const;
    uint32_t remainingTtl() const;
    Client *findClient(int fd);
    bool authenticateFd(int fd);
    bool addClient(int fd);
    void dropClient(int fd, const char *reason);
    void releaseLease(const char *reason);
    void acceptOne();
    void handleClient(int fd);
    bool writePayload(int fd, std::string_view payload);
    void dispatch(Client &client, const IpcRequest &request);

    SignalEpollWait &wait_;
    IArmControl &arm_;
    ISessionAuthorizer &auth_;
    IPeerCredentials &creds_;
    Logger &logger_;
    const uint64_t *nowOverride_ = nullptr;
    int listenFd_ = -1;
    int leaseFd_ = -1;
    uint32_t leaseTtlMs_ = kDefaultLeaseMs;
    uint64_t leaseExpiryMs_ = 0;
    std::string path_;
    std::vector<Client> clients_;
};

} // namespace contextdeck::broker
