/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "Protocol.hxx"
#include "Builder.hxx"
#include "Parser.hxx"
#include "Prepared.hxx"
#include "mount_list.hxx"
#include "ExitListener.hxx"
#include "system/Error.hxx"
#include "util/RuntimeError.hxx"
#include "util/ScopeExit.hxx"

#include <daemon/log.h>

#include <array>

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>

static constexpr size_t MAX_FDS = 8;

SpawnServerClient::SpawnServerClient(EventLoop &event_loop,
                                     const SpawnConfig &_config, int _fd)
    :config(_config), fd(_fd),
     read_event(event_loop, fd, EV_READ|EV_PERSIST,
                BIND_THIS_METHOD(OnSocketEvent))
{
    read_event.Add();
}

SpawnServerClient::~SpawnServerClient()
{
    if (fd >= 0)
        Close();
}

void
SpawnServerClient::ReplaceSocket(int new_fd)
{
    assert(fd >= 0);
    assert(new_fd >= 0);
    assert(fd != new_fd);
    assert(!shutting_down);

    processes.clear();

    Close();

    fd = new_fd;

    read_event.Set(fd, EV_READ|EV_PERSIST);
    read_event.Add();
}

void
SpawnServerClient::Close()
{
    assert(fd >= 0);

    read_event.Delete();
    close(fd);
    fd = -1;
}

void
SpawnServerClient::Shutdown()
{
    shutting_down = true;

    if (processes.empty() && fd >= 0)
        Close();
}

void
SpawnServerClient::CheckOrAbort()
{
    if (fd < 0) {
        daemon_log(1, "SpawnChildProcess: the spawner is gone, emergency!\n");
        exit(EXIT_FAILURE);
    }
}

inline void
SpawnServerClient::Send(ConstBuffer<void> payload, ConstBuffer<int> fds)
{
    ::Send<MAX_FDS>(fd, payload, fds);
}

inline void
SpawnServerClient::Send(const SpawnSerializer &s)
{
    ::Send<MAX_FDS>(fd, s);
}

int
SpawnServerClient::Connect()
{
    CheckOrAbort();

    int sv[2];
    if (socketpair(AF_LOCAL, SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,
                   0, sv) < 0)
        throw MakeErrno("socketpair() failed");

    const int local_fd = sv[0];
    const int remote_fd = sv[1];

    AtScopeExit(remote_fd){ close(remote_fd); };

    static constexpr SpawnRequestCommand cmd = SpawnRequestCommand::CONNECT;

    try {
        Send(ConstBuffer<void>(&cmd, sizeof(cmd)), {&remote_fd, 1});
    } catch (...) {
        close(local_fd);
        std::throw_with_nested(std::runtime_error("Spawn server failed"));
    }

    return local_fd;
}

static void
Serialize(SpawnSerializer &s, const CgroupOptions &c)
{
    s.WriteOptionalString(SpawnExecCommand::CGROUP, c.name);

    for (const auto *set = c.set_head; set != nullptr; set = set->next) {
        s.Write(SpawnExecCommand::CGROUP_SET);
        s.WriteString(set->name);
        s.WriteString(set->value);
    }
}

static void
Serialize(SpawnSerializer &s, const RefenceOptions &_r)
{
    const auto r = _r.Get();
    if (!r.IsNull()) {
        s.Write(SpawnExecCommand::REFENCE);
        s.Write(r.ToVoid());
        s.WriteByte(0);
    }
}

static void
Serialize(SpawnSerializer &s, const NamespaceOptions &ns)
{
    s.WriteOptional(SpawnExecCommand::USER_NS, ns.enable_user);
    s.WriteOptional(SpawnExecCommand::PID_NS, ns.enable_pid);
    s.WriteOptional(SpawnExecCommand::NETWORK_NS, ns.enable_network);
    s.WriteOptional(SpawnExecCommand::IPC_NS, ns.enable_ipc);
    s.WriteOptional(SpawnExecCommand::MOUNT_NS, ns.enable_mount);
    s.WriteOptional(SpawnExecCommand::MOUNT_PROC, ns.mount_proc);
    s.WriteOptionalString(SpawnExecCommand::PIVOT_ROOT, ns.pivot_root);

    if (ns.mount_home != nullptr) {
        s.Write(SpawnExecCommand::MOUNT_HOME);
        s.WriteString(ns.mount_home);
        s.WriteString(ns.home);
    }

    s.WriteOptionalString(SpawnExecCommand::MOUNT_TMP_TMPFS, ns.mount_tmp_tmpfs);
    s.WriteOptionalString(SpawnExecCommand::MOUNT_TMPFS, ns.mount_tmpfs);

    for (auto i = ns.mounts; i != nullptr; i = i->next) {
        s.Write(SpawnExecCommand::BIND_MOUNT);
        s.WriteString(i->source);
        s.WriteString(i->target);
        s.WriteByte(i->writable);
        s.WriteByte(i->exec);
    }

    s.WriteOptionalString(SpawnExecCommand::HOSTNAME, ns.hostname);
}

static void
Serialize(SpawnSerializer &s, unsigned i, const ResourceLimit &rlimit)
{
    if (rlimit.IsEmpty())
        return;

    s.Write(SpawnExecCommand::RLIMIT);
    s.WriteByte(i);

    const struct rlimit &data = rlimit;
    s.WriteT(data);
}

static void
Serialize(SpawnSerializer &s, const ResourceLimits &rlimits)
{
    for (unsigned i = 0; i < RLIM_NLIMITS; ++i)
        Serialize(s, i, rlimits.values[i]);
}

static void
Serialize(SpawnSerializer &s, const UidGid &uid_gid)
{
    if (uid_gid.IsEmpty())
        return;

    s.Write(SpawnExecCommand::UID_GID);
    s.WriteT(uid_gid.uid);
    s.WriteT(uid_gid.gid);

    const size_t n_groups = uid_gid.CountGroups();
    s.WriteByte(n_groups);
    for (size_t i = 0; i < n_groups; ++i)
        s.WriteT(uid_gid.groups[i]);
}

static void
Serialize(SpawnSerializer &s, const PreparedChildProcess &p)
{
    for (const char *i : p.args)
        s.WriteString(SpawnExecCommand::ARG, i);

    for (const char *i : p.env)
        s.WriteString(SpawnExecCommand::SETENV, i);

    s.CheckWriteFd(SpawnExecCommand::STDIN, p.stdin_fd);
    s.CheckWriteFd(SpawnExecCommand::STDOUT, p.stdout_fd);
    s.CheckWriteFd(SpawnExecCommand::STDERR, p.stderr_fd);
    s.CheckWriteFd(SpawnExecCommand::CONTROL, p.control_fd);

    if (p.priority != 0) {
        s.Write(SpawnExecCommand::PRIORITY);
        s.WriteInt(p.priority);
    }

    Serialize(s, p.cgroup);
    Serialize(s, p.refence);
    Serialize(s, p.ns);
    Serialize(s, p.rlimits);
    Serialize(s, p.uid_gid);

    s.WriteOptionalString(SpawnExecCommand::CHROOT, p.chroot);

    if (p.no_new_privs)
        s.Write(SpawnExecCommand::NO_NEW_PRIVS);
}

int
SpawnServerClient::SpawnChildProcess(const char *name,
                                     PreparedChildProcess &&p,
                                     ExitListener *listener)
{
    assert(!shutting_down);

    /* this check is performed again on the server (which is obviously
       necessary, and the only way to have it secure); this one is
       only here for the developer to see the error earlier in the
       call chain */
    if (!p.uid_gid.IsEmpty() && !config.Verify(p.uid_gid))
        throw FormatRuntimeError("uid/gid not allowed: %d/%d",
                                 int(p.uid_gid.uid), int(p.uid_gid.gid));

    CheckOrAbort();

    const int pid = MakePid();

    SpawnSerializer s(SpawnRequestCommand::EXEC);

    try {
        s.WriteInt(pid);
        s.WriteString(name);

        Serialize(s, p);
    } catch (SpawnPayloadTooLargeError) {
        throw std::runtime_error("Spawn payload is too large");
    }

    try {
        Send(s.GetPayload(), s.GetFds());
    } catch (const std::runtime_error &e) {
        std::throw_with_nested(std::runtime_error("Spawn server failed"));
    }

    processes.emplace(std::piecewise_construct,
                      std::forward_as_tuple(pid),
                      std::forward_as_tuple(listener));
    return pid;
}

void
SpawnServerClient::SetExitListener(int pid, ExitListener *listener)
{
    auto i = processes.find(pid);
    assert(i != processes.end());

    assert(i->second.listener == nullptr);
    i->second.listener = listener;
}

void
SpawnServerClient::KillChildProcess(int pid, int signo)
{
    CheckOrAbort();

    auto i = processes.find(pid);
    assert(i != processes.end());
    assert(i->second.listener != nullptr);
    processes.erase(i);

    SpawnSerializer s(SpawnRequestCommand::KILL);
    s.WriteInt(pid);
    s.WriteInt(signo);

    try {
        Send(s.GetPayload(), s.GetFds());
    } catch (const std::runtime_error &e) {
        daemon_log(1, "failed to send KILL(%d) to spawner: %s\n",
                   pid,e.what());
    }

    if (shutting_down && processes.empty())
        Close();
}

inline void
SpawnServerClient::HandleExitMessage(SpawnPayload payload)
{
    int pid, status;
    payload.ReadInt(pid);
    payload.ReadInt(status);
    if (!payload.IsEmpty())
        throw MalformedSpawnPayloadError();

    auto i = processes.find(pid);
    if (i == processes.end())
        return;

    auto *listener = i->second.listener;
    processes.erase(i);

    if (listener != nullptr)
        listener->OnChildProcessExit(status);

    if (shutting_down && processes.empty())
        Close();
}

inline void
SpawnServerClient::HandleMessage(ConstBuffer<uint8_t> payload)
{
    const auto cmd = (SpawnResponseCommand)payload.shift();

    switch (cmd) {
    case SpawnResponseCommand::EXIT:
        HandleExitMessage(SpawnPayload(payload));
        break;
    }
}

inline void
SpawnServerClient::OnSocketEvent(unsigned)
{
    constexpr size_t N = 64;
    std::array<uint8_t[16], N> payloads;
    std::array<struct iovec, N> iovs;
    std::array<struct mmsghdr, N> msgs;

    for (size_t i = 0; i < N; ++i) {
        auto &iov = iovs[i];
        iov.iov_base = payloads[i];
        iov.iov_len = sizeof(payloads[i]);

        auto &msg = msgs[i].msg_hdr;
        msg.msg_name = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
    }

    int n = recvmmsg(fd, &msgs.front(), msgs.size(),
                     MSG_DONTWAIT|MSG_CMSG_CLOEXEC, nullptr);
    if (n <= 0) {
        if (n < 0)
            daemon_log(2, "recvmsg() from spawner failed: %s\n",
                       strerror(errno));
        else
            daemon_log(2, "spawner closed the socket\n");
        Close();
        return;
    }

    for (int i = 0; i < n; ++i) {
        if (msgs[i].msg_len == 0) {
            /* when the peer closes the socket, recvmmsg() doesn't
               return 0; insteaed, it fills the mmsghdr array with
               empty packets */
            daemon_log(2, "spawner closed the socket\n");
            Close();
            return;
        }

        HandleMessage({payloads[i], msgs[i].msg_len});
    }
}
