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
#include "pevent.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/Macros.hxx"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct DelegateClient {
    struct lease_ref lease_ref;
    const int fd;
    struct event event;

    struct pool *const pool;

    const struct delegate_handler *const handler;
    void *handler_ctx;

    struct async_operation operation;

    DelegateClient(int _fd, const struct lease &lease, void *lease_ctx,
                   struct pool &_pool,
                   const struct delegate_handler &_handler, void *_handler_ctx)
        :fd(_fd), pool(&_pool),
         handler(&_handler), handler_ctx(_handler_ctx) {
         p_lease_ref_set(lease_ref, lease, lease_ctx,
                         _pool, "delegate_client_lease");
    }

    ~DelegateClient() {
        pool_unref(pool);
    }

    void Destroy() {
        this->~DelegateClient();
    }
};

static void
delegate_release_socket(DelegateClient *d, bool reuse)
{
    assert(d != nullptr);
    assert(d->fd >= 0);

    p_lease_release(d->lease_ref, reuse, *d->pool);
}

static void
delegate_handle_fd(DelegateClient *d, const struct msghdr *msg,
                   size_t length)
{
    if (length != 0) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "Invalid message length");
        d->handler->error(error, d->handler_ctx);
        d->Destroy();
        return;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
    if (cmsg == nullptr) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "No fd passed");
        d->handler->error(error, d->handler_ctx);
        d->Destroy();
        return;
    }

    if (cmsg->cmsg_type != SCM_RIGHTS) {
        delegate_release_socket(d, false);

        GError *error = g_error_new(delegate_client_quark(), 0,
                                    "got control message of unknown type %d",
                                    cmsg->cmsg_type);
        d->handler->error(error, d->handler_ctx);
        d->Destroy();
        return;
    }

    delegate_release_socket(d, true);

    const void *data = CMSG_DATA(cmsg);
    const int *fd_p = (const int *)data;

    int fd = *fd_p;
    d->handler->success(fd, d->handler_ctx);
    d->Destroy();
}

static void
delegate_handle_errno(DelegateClient *d,
                      size_t length)
{
    int e;

    if (length != sizeof(e)) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "Invalid message length");
        d->handler->error(error, d->handler_ctx);
        d->Destroy();
        return;
    }

    ssize_t nbytes = recv(d->fd, &e, sizeof(e), 0);
    GError *error;

    if (nbytes == sizeof(e)) {
        delegate_release_socket(d, true);

        error = new_error_errno2(e);
    } else {
        delegate_release_socket(d, false);

        error = g_error_new_literal(delegate_client_quark(), 0,
                                    "Failed to receive errno");
    }

    d->handler->error(error, d->handler_ctx);
    d->Destroy();
}

static void
delegate_handle_msghdr(DelegateClient *d, const struct msghdr *msg,
                       DelegateResponseCommand command, size_t length)
{
    switch (command) {
    case DelegateResponseCommand::FD:
        delegate_handle_fd(d, msg, length);
        return;

    case DelegateResponseCommand::ERRNO:
        /* i/o error */
        delegate_handle_errno(d, length);
        return;
    }

    delegate_release_socket(d, false);
    GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                        "Invalid delegate response");
    d->handler->error(error, d->handler_ctx);
    d->Destroy();
}

static void
delegate_try_read(DelegateClient *d)
{
    d->operation.Finished();

    struct iovec iov;
    int fd;
    char ccmsg[CMSG_SPACE(sizeof(fd))];
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

    nbytes = recvmsg_cloexec(d->fd, &msg, 0);
    if (nbytes < 0) {
        fd = -errno;

        delegate_release_socket(d, false);

        GError *error = new_error_errno_msg("recvmsg() failed");
        d->handler->error(error, d->handler_ctx);
        d->Destroy();
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "short recvmsg()");
        d->handler->error(error, d->handler_ctx);
        d->Destroy();
        return;
    }

    delegate_handle_msghdr(d, &msg, header.command,
                           header.length);
}

static void
delegate_read_event_callback(int fd gcc_unused, short event gcc_unused,
                              void *ctx)
{
    DelegateClient *d = (DelegateClient *)ctx;

    p_event_consumed(&d->event, d->pool);

    assert(d->fd == fd);

    delegate_try_read(d);
}

/*
 * async operation
 *
 */

static void
delegate_connection_abort(struct async_operation *ao)
{
    DelegateClient &d = ContainerCast2(*ao, &DelegateClient::operation);

    p_event_del(&d.event, d.pool);
    delegate_release_socket(&d, false);
    pool_unref(d.pool);
}

static const struct async_operation_class delegate_operation = {
    .abort = delegate_connection_abort,
};


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

    d->operation.Init(delegate_operation);
    async_ref->Set(d->operation);

    event_set(&d->event, d->fd, EV_READ,
              delegate_read_event_callback, d);
    p_event_add(&d->event, nullptr, pool, "delegate_client_event");
}
