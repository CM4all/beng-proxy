/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "Handler.hxx"
#include "Protocol.hxx"
#include "please.hxx"
#include "system/fd_util.h"
#include "gerrno.h"
#include "pool.hxx"
#include "event/SocketEvent.hxx"
#include "util/Cancellable.hxx"
#include "util/Macros.hxx"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct DelegateClient final : Cancellable {
    struct lease_ref lease_ref;
    const int fd;
    SocketEvent event;

    struct pool &pool;

    DelegateHandler &handler;

    DelegateClient(EventLoop &event_loop, int _fd, Lease &lease,
                   struct pool &_pool,
                   DelegateHandler &_handler)
        :fd(_fd), event(event_loop, fd, SocketEvent::READ,
                        BIND_THIS_METHOD(SocketEventCallback)),
         pool(_pool),
         handler(_handler) {
        p_lease_ref_set(lease_ref, lease,
                        _pool, "delegate_client_lease");

        event.Add();
    }

    ~DelegateClient() {
        pool_unref(&pool);
    }

    void Destroy() {
        this->~DelegateClient();
    }

    void ReleaseSocket(bool reuse) {
        assert(fd >= 0);

        p_lease_release(lease_ref, reuse, pool);
    }

    void DestroyError(GError *error) {
        ReleaseSocket(false);
        handler.OnDelegateError(error);
        Destroy();
    }

    void HandleFd(const struct msghdr &msg, size_t length);
    void HandleErrno(size_t length);
    void HandleMsg(const struct msghdr &msg,
                   DelegateResponseCommand command, size_t length);
    void TryRead();

private:
    void SocketEventCallback(unsigned) {
        TryRead();
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        event.Delete();
        ReleaseSocket(false);
        Destroy();
    }
};

gcc_const
static inline GQuark
delegate_client_quark(void)
{
    return g_quark_from_static_string("delegate_client");
}

inline void
DelegateClient::HandleFd(const struct msghdr &msg, size_t length)
{
    if (length != 0) {
        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "Invalid message length");
        DestroyError(error);
        return;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == nullptr) {
        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "No fd passed");
        DestroyError(error);
        return;
    }

    if (cmsg->cmsg_type != SCM_RIGHTS) {
        GError *error = g_error_new(delegate_client_quark(), 0,
                                    "got control message of unknown type %d",
                                    cmsg->cmsg_type);
        DestroyError(error);
        return;
    }

    ReleaseSocket(true);

    const void *data = CMSG_DATA(cmsg);
    const int *fd_p = (const int *)data;

    int new_fd = *fd_p;
    handler.OnDelegateSuccess(new_fd);
    Destroy();
}

inline void
DelegateClient::HandleErrno(size_t length)
{
    int e;

    if (length != sizeof(e)) {
        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "Invalid message length");
        DestroyError(error);
        return;
    }

    ssize_t nbytes = recv(fd, &e, sizeof(e), 0);
    GError *error;

    if (nbytes == sizeof(e)) {
        ReleaseSocket(true);

        error = new_error_errno2(e);
    } else {
        ReleaseSocket(false);

        error = g_error_new_literal(delegate_client_quark(), 0,
                                    "Failed to receive errno");
    }

    handler.OnDelegateError(error);
    Destroy();
}

inline void
DelegateClient::HandleMsg(const struct msghdr &msg,
                          DelegateResponseCommand command, size_t length)
{
    switch (command) {
    case DelegateResponseCommand::FD:
        HandleFd(msg, length);
        return;

    case DelegateResponseCommand::ERRNO:
        /* i/o error */
        HandleErrno(length);
        return;
    }

    GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                        "Invalid delegate response");
    DestroyError(error);
}

inline void
DelegateClient::TryRead()
{
    struct iovec iov;
    int new_fd;
    char ccmsg[CMSG_SPACE(sizeof(new_fd))];
    struct msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = ccmsg,
        .msg_controllen = sizeof(ccmsg),
    };
    DelegateResponseHeader header;
    ssize_t nbytes;

    iov.iov_base = &header;
    iov.iov_len = sizeof(header);

    nbytes = recvmsg_cloexec(fd, &msg, 0);
    if (nbytes < 0) {
        GError *error = new_error_errno_msg("recvmsg() failed");
        DestroyError(error);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "short recvmsg()");
        DestroyError(error);
        return;
    }

    HandleMsg(msg, header.command, header.length);
}

/*
 * constructor
 *
 */

static bool
SendDelegatePacket(int fd, DelegateRequestCommand cmd,
                   const void *payload, size_t length,
                   GError **error_r)
{
    const DelegateRequestHeader header = {
        .length = (uint16_t)length,
        .command = cmd,
    };

    struct iovec v[] = {
        { const_cast<void *>((const void *)&header), sizeof(header) },
        { const_cast<void *>(payload), length },
    };

    struct msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = v,
        .msg_iovlen = ARRAY_SIZE(v),
        .msg_control = nullptr,
        .msg_controllen = 0,
        .msg_flags = 0,
    };

    auto nbytes = sendmsg(fd, &msg, MSG_DONTWAIT);
    if (nbytes < 0) {
        set_error_errno_msg(error_r, "Failed to send to delegate");
        return false;
    }

    if (size_t(nbytes) != sizeof(header) + length) {
        g_set_error_literal(error_r, delegate_client_quark(), 0,
                            "Short send to delegate");
        return false;
    }

    return true;
}

void
delegate_open(EventLoop &event_loop, int fd, Lease &lease,
              struct pool *pool, const char *path,
              DelegateHandler &handler,
              CancellablePointer &cancel_ptr)
{
    GError *error = nullptr;
    if (!SendDelegatePacket(fd, DelegateRequestCommand::OPEN,
                            path, strlen(path),
                            &error)) {
        lease.ReleaseLease(false);
        handler.OnDelegateError(error);
        return;
    }

    auto d = NewFromPool<DelegateClient>(*pool, event_loop, fd, lease,
                                         *pool,
                                         handler);

    pool_ref(pool);

    cancel_ptr = *d;
}
