/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"
#include "Config.hxx"
#include "Protocol.hxx"
#include "Parser.hxx"
#include "Builder.hxx"
#include "Prepared.hxx"
#include "mount_list.hxx"
#include "Direct.hxx"
#include "Registry.hxx"
#include "ExitListener.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StaticArray.hxx"

#include <daemon/log.h>

#include <boost/intrusive/list.hpp>

#include <algorithm>
#include <memory>
#include <map>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

class SpawnServerProcess;

class SpawnFd {
    int fd;

public:
    explicit SpawnFd(int _fd=-1):fd(_fd) {}

    SpawnFd(SpawnFd &&src):fd(src.fd) {
        src.fd = -1;
    }

    ~SpawnFd() {
        if (fd >= 0)
            close(fd);
    }

    SpawnFd &operator=(SpawnFd &&src) {
        std::swap(fd, src.fd);
        return *this;
    }

    int Release() {
        int result = fd;
        fd = -1;
        return result;
    }
};

class SpawnFdList {
    ConstBuffer<int> list;

public:
    SpawnFdList(std::nullptr_t n):list(n) {}

    explicit SpawnFdList(ConstBuffer<int> _list)
        :list(_list) {}

    SpawnFdList(SpawnFdList &&src)
        :list(src.list) {
        src.list = nullptr;
    }

    ~SpawnFdList() {
        for (auto fd : list)
            close(fd);
    }

    SpawnFdList &operator=(SpawnFdList &&src) {
        std::swap(list, src.list);
        return *this;
    }

    bool IsEmpty() {
        return list.IsEmpty();
    }

    size_t size() const {
        return list.size;
    }

    SpawnFd Get() {
        if (IsEmpty())
            throw MalformedSpawnPayloadError();

        return SpawnFd(list.shift());
    }
};

class SpawnServerConnection;

class SpawnServerChild final : public ExitListener {
    SpawnServerConnection &connection;

    const int id;

    const pid_t pid;

    const std::string name;

public:
    explicit SpawnServerChild(SpawnServerConnection &_connection,
                              int _id, pid_t _pid,
                              const char *_name)
        :connection(_connection), id(_id), pid(_pid), name(_name) {}

    SpawnServerChild(const SpawnServerChild &) = delete;
    SpawnServerChild &operator=(const SpawnServerChild &) = delete;

    const char *GetName() const {
        return name.c_str();
    }

    void Kill(ChildProcessRegistry &child_process_registry, int signo) {
        child_process_registry.Kill(pid, signo);
    }

    /* virtual methods from ExitListener */
    void OnChildProcessExit(int status) override;

    /* boost::instrusive::set hooks */
    typedef boost::intrusive::set_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> IdHook;
    IdHook id_hook;

    struct CompareId {
        bool operator()(const SpawnServerChild &a, const SpawnServerChild &b) const {
            return a.id < b.id;
        }

        bool operator()(int a, const SpawnServerChild &b) const {
            return a < b.id;
        }

        bool operator()(const SpawnServerChild &a, int b) const {
            return a.id < b;
        }
    };
};

class SpawnServerConnection
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
    SpawnServerProcess &process;
    const int fd;

    Event event;

    typedef boost::intrusive::set<SpawnServerChild,
                                  boost::intrusive::member_hook<SpawnServerChild,
                                                                SpawnServerChild::IdHook,
                                                                &SpawnServerChild::id_hook>,
                                  boost::intrusive::compare<SpawnServerChild::CompareId>> ChildIdMap;
    ChildIdMap children;

public:
    SpawnServerConnection(SpawnServerProcess &_process, int _fd);
    ~SpawnServerConnection();

    void OnChildProcessExit(int id, int status, SpawnServerChild *child);

private:
    void RemoveConnection();

    void SendExit(int id, int status);
    void SpawnChild(int id, const char *name, PreparedChildProcess &&p);

    void HandleExecMessage(SpawnPayload payload, SpawnFdList &&fds);
    void HandleKillMessage(SpawnPayload payload, SpawnFdList &&fds);
    void HandleMessage(ConstBuffer<uint8_t> payload, SpawnFdList &&fds);
    void HandleMessage(const struct msghdr &msg, ConstBuffer<uint8_t> payload);

    void ReadEventCallback();
};

void
SpawnServerChild::OnChildProcessExit(int status)
{
    connection.OnChildProcessExit(id, status, this);
}

void
SpawnServerConnection::OnChildProcessExit(int id, int status,
                                          SpawnServerChild *child)
{
    children.erase(children.iterator_to(*child));
    delete child;

    SendExit(id, status);
}

class SpawnServerProcess {
    const SpawnConfig config;

    EventBase base;

    ChildProcessRegistry child_process_registry;

    typedef boost::intrusive::list<SpawnServerConnection,
                                   boost::intrusive::constant_time_size<false>> ConnectionList;
    ConnectionList connections;

public:
    explicit SpawnServerProcess(const SpawnConfig &_config)
        :config(_config) {}

    const SpawnConfig &GetConfig() const {
        return config;
    }

    EventBase &GetEventBase() {
        return base;
    }

    ChildProcessRegistry &GetChildProcessRegistry() {
        return child_process_registry;
    }

    void AddConnection(int fd) {
        auto connection = new SpawnServerConnection(*this, fd);
        connections.push_back(*connection);
    }

    void RemoveConnection(SpawnServerConnection &connection) {
        connections.erase_and_dispose(connections.iterator_to(connection),
                                      DeleteDisposer());

        if (connections.empty())
            /* all connections are gone */
            Quit();
    }

    void Run();

private:
    void Quit() {
        assert(connections.empty());

        child_process_registry.SetVolatile();
    }
};

SpawnServerConnection::SpawnServerConnection(SpawnServerProcess &_process,
                                             int _fd)
    :process(_process), fd(_fd) {
    event.Set(process.GetEventBase(), fd, EV_READ|EV_PERSIST,
              MakeSimpleEventCallback(SpawnServerConnection,
                                      ReadEventCallback),
              this);
    event.Add();
}

SpawnServerConnection::~SpawnServerConnection()
{
    event.Delete();
    close(fd);

    auto &registry = process.GetChildProcessRegistry();
    children.clear_and_dispose([&registry](SpawnServerChild *child){
            child->Kill(registry, SIGTERM);
            delete child;
        });
}

inline void
SpawnServerConnection::RemoveConnection()
{
    process.RemoveConnection(*this);
}

void
SpawnServerConnection::SendExit(int id, int status)
{
    SpawnSerializer s(SpawnResponseCommand::EXIT);
    s.WriteInt(id);
    s.WriteInt(status);

    try {
        ::Send<1>(fd, s);
    } catch (const std::runtime_error &e) {
        daemon_log(1, "Failed to send EXIT to worker: %s\n",
                   e.what());
        RemoveConnection();
    }
}

inline void
SpawnServerConnection::SpawnChild(int id, const char *name,
                                  PreparedChildProcess &&p)
{
    // TODO: uid/gid?
    pid_t pid = SpawnChildProcess(std::move(p), process.GetConfig());
    if (pid < 0) {
        daemon_log(1, "Failed to spawn child process: %s\n", strerror(-pid));
        SendExit(id, W_EXITCODE(0xff, 0));
        return;
    }

    auto *child = new SpawnServerChild(*this, id, pid, name);
    children.insert(*child);

    process.GetChildProcessRegistry().Add(pid, name, child);
}

inline void
SpawnServerConnection::HandleExecMessage(SpawnPayload payload,
                                         SpawnFdList &&fds)
{
    int id;
    payload.ReadInt(id);
    const char *name = payload.ReadString();

    PreparedChildProcess p;

    MountList **mount_tail = &p.ns.mounts;
    assert(*mount_tail == nullptr);

    std::forward_list<MountList> mounts;

    while (!payload.IsEmpty()) {
        const SpawnExecCommand cmd = (SpawnExecCommand)payload.ReadByte();
        switch (cmd) {
        case SpawnExecCommand::ARG:
            if (!p.Append(payload.ReadString()))
                throw MalformedSpawnPayloadError();
            break;

        case SpawnExecCommand::SETENV:
            if (!p.PutEnv(payload.ReadString()))
                throw MalformedSpawnPayloadError();
            break;

        case SpawnExecCommand::STDIN:
            p.SetStdin(fds.Get().Release());
            break;

        case SpawnExecCommand::STDOUT:
            p.SetStdout(fds.Get().Release());
            break;

        case SpawnExecCommand::STDERR:
            p.SetStderr(fds.Get().Release());
            break;

        case SpawnExecCommand::CONTROL:
            p.SetControl(fds.Get().Release());
            break;

        case SpawnExecCommand::REFENCE:
            p.refence.Set(payload.ReadString());
            break;

        case SpawnExecCommand::USER_NS:
            p.ns.enable_user = true;
            break;

        case SpawnExecCommand::PID_NS:
            p.ns.enable_pid = true;
            break;

        case SpawnExecCommand::NETWORK_NS:
            p.ns.enable_network = true;
            break;

        case SpawnExecCommand::IPC_NS:
            p.ns.enable_ipc = true;
            break;

        case SpawnExecCommand::MOUNT_NS:
            p.ns.enable_mount = true;
            break;

        case SpawnExecCommand::MOUNT_PROC:
            p.ns.mount_proc = true;
            break;

        case SpawnExecCommand::PIVOT_ROOT:
            p.ns.pivot_root = payload.ReadString();
            break;

        case SpawnExecCommand::MOUNT_HOME:
            p.ns.mount_home = payload.ReadString();
            p.ns.home = payload.ReadString();
            break;

        case SpawnExecCommand::MOUNT_TMP_TMPFS:
            p.ns.mount_tmp_tmpfs = payload.ReadString();
            break;

        case SpawnExecCommand::MOUNT_TMPFS:
            p.ns.mount_tmpfs = payload.ReadString();
            break;

        case SpawnExecCommand::BIND_MOUNT:
            {
                const char *source = payload.ReadString();
                const char *target = payload.ReadString();
                bool writable = payload.ReadByte();
                mounts.emplace_front(source, target, false,
                                     writable);
            }

            *mount_tail = &mounts.front();
            mount_tail = &mounts.front().next;
            break;

        case SpawnExecCommand::HOSTNAME:
            p.ns.hostname = payload.ReadString();
            break;

        case SpawnExecCommand::RLIMIT:
            {
                unsigned i = payload.ReadByte();
                struct rlimit &data = p.rlimits.values[i];
                payload.Read(&data, sizeof(data));
            }
            break;
        }
    }

    SpawnChild(id, name, std::move(p));
}

inline void
SpawnServerConnection::HandleKillMessage(SpawnPayload payload,
                                         SpawnFdList &&fds)
{
    if (!fds.IsEmpty())
        throw MalformedSpawnPayloadError();

    int id, signo;
    payload.ReadInt(id);
    payload.ReadInt(signo);
    if (!payload.IsEmpty())
        throw MalformedSpawnPayloadError();

    auto i = children.find(id, SpawnServerChild::CompareId());
    if (i == children.end())
        return;

    SpawnServerChild *child = &*i;
    children.erase(i);

    child->Kill(process.GetChildProcessRegistry(), signo);
    delete child;
}

inline void
SpawnServerConnection::HandleMessage(ConstBuffer<uint8_t> payload,
                                     SpawnFdList &&fds)
{
    const auto cmd = (SpawnRequestCommand)payload.shift();

    switch (cmd) {
    case SpawnRequestCommand::CONNECT:
        if (!payload.IsEmpty() || fds.size() != 1)
            throw MalformedSpawnPayloadError();

        process.AddConnection(fds.Get().Release());
        break;

    case SpawnRequestCommand::EXEC:
        HandleExecMessage(SpawnPayload(payload), std::move(fds));
        break;

    case SpawnRequestCommand::KILL:
        HandleKillMessage(SpawnPayload(payload), std::move(fds));
        break;
    }
}

inline void
SpawnServerConnection::HandleMessage(const struct msghdr &msg,
                                     ConstBuffer<uint8_t> payload)
{
    SpawnFdList fds = nullptr;

    const struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg != nullptr && cmsg->cmsg_level == SOL_SOCKET &&
        cmsg->cmsg_type == SCM_RIGHTS) {
        auto data = ConstBuffer<void>(CMSG_DATA(cmsg),
                                      cmsg->cmsg_len - CMSG_LEN(0));
        fds = SpawnFdList(ConstBuffer<int>::FromVoid(data));
    }

    HandleMessage(payload, std::move(fds));
}

inline void
SpawnServerConnection::ReadEventCallback()
{
    uint8_t payload[8192];

    struct iovec iov;
    iov.iov_base = payload;
    iov.iov_len = sizeof(payload);

    int fds[32];
    char ccmsg[CMSG_SPACE(sizeof(fds))];
    struct msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = ccmsg,
        .msg_controllen = sizeof(ccmsg),
    };

    ssize_t nbytes = recvmsg(fd, &msg, MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
    if (nbytes <= 0) {
        if (nbytes < 0)
            daemon_log(2, "recvmsg() failed: %s\n", strerror(errno));
        RemoveConnection();
        return;
    }

    try {
        HandleMessage(msg, {payload, size_t(nbytes)});
    } catch (MalformedSpawnPayloadError) {
        daemon_log(3, "Malformed spawn payload\n");
    }
}

inline void
SpawnServerProcess::Run()
{
    base.Dispatch();
}

void
RunSpawnServer(const SpawnConfig &config, int fd)
{
    SpawnServerProcess process(config);
    process.AddConnection(fd);
    process.Run();
}
