/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "Handler.hxx"
#include "Protocol.hxx"
#include "async.hxx"
#include "please.hxx"
#include "fd_util.h"
#include "gerrno.h"
#include "pool.hxx"
#include "event/Event.hxx"
#include "util/Macros.hxx"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct DelegateClient {
    struct lease_ref lease_ref;
    const int fd;
    Event event;

    struct pool &pool;

    const struct delegate_handler &handler;
    void *handler_ctx;

    struct async_operation operation;

    DelegateClient(int _fd, const struct lease &lease, void *lease_ctx,
                   struct pool &_pool,
                   const struct delegate_handler &_handler, void *_handler_ctx)
        :fd(_fd), pool(_pool),
         handler(_handler), handler_ctx(_handler_ctx) {
         p_lease_ref_set(lease_ref, lease, lease_ctx,
                         _pool, "delegate_client_lease");
         operation.Init2<DelegateClient, &DelegateClient::operation>();
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
        handler.error(error, handler_ctx);
        Destroy();
    }

    void HandleFd(const struct msghdr &msg, size_t length);
    void HandleErrno(size_t length);
    void HandleMsg(const struct msghdr &msg,
                   DelegateResponseCommand command, size_t length);
    void TryRead();

    void Abort() {
        event.Delete();
        ReleaseSocket(false);
        Destroy();
    }
};

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
    handler.success(new_fd, handler_ctx);
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

    handler.error(error, handler_ctx);
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
    operation.Finished();

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

static void
delegate_read_event_callback(int fd gcc_unused, short event gcc_unused,
                             void *ctx)
{
    DelegateClient *d = (DelegateClient *)ctx;

    assert(d->fd == fd);

    d->TryRead();
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
delegate_open(int fd, const struct lease *lease, void *lease_ctx,
              struct pool *pool, const char *path,
              const struct delegate_handler *handler, void *ctx,
              struct async_operation_ref *async_ref)
{
    GError *error = nullptr;
    if (!SendDelegatePacket(fd, DelegateRequestCommand::OPEN,
                            path, strlen(path),
                            &error)) {
        lease->Release(lease_ctx, false);
        handler->error(error, ctx);
        return;
    }

    auto d = NewFromPool<DelegateClient>(*pool, fd, *lease, lease_ctx,
                                         *pool,
                                         *handler, ctx);

    pool_ref(pool);

    async_ref->Set(d->operation);

    d->event.Set(d->fd, EV_READ,
                 delegate_read_event_callback, d);
    d->event.Add();
}
