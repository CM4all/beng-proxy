/*
 * Glue for libnfs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_client.hxx"
#include "pool.hxx"
#include "async.hxx"
#include "hashmap.hxx"
#include "gerrno.h"
#include "system/fd_util.h"
#include "util/Cast.hxx"

#include <inline/list.h>

extern "C" {
#include <nfsc/libnfs.h>
}

#include <event.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/stat.h>

/**
 * A handle that is passed to the caller.  Each file can have multiple
 * public "handles", one for each caller.  That way, only one #nfsfh
 * (inside #nfs_file) is needed.
 */
struct NfsFileHandle {
    /**
     * @see nfs_file::handles
     */
    struct list_head siblings;

    struct nfs_file *file;

    /**
     * A child of #nfs_file::pool.
     */
    struct pool *pool;

    /**
     * The pool provided by the caller.  It must be referenced until
     * the response has been delivered.
     */
    struct pool *caller_pool;

    enum {
        /**
         * Waiting for the file to be opened.  The
         * nfs_client_open_file_handler will be invoked next.
         */
        WAITING,

        /**
         * The file is ready, the nfs_client_open_file_handler has
         * been invoked already.
         */
        IDLE,

        /**
         * A request by this handle is pending inside libnfs.  This
         * object can only be freed when all libnfs operations
         * referencing this object are finished.
         */
        PENDING,

        /**
         * istream_close() has been called by the istream handler
         * while the state was #PENDING.  This object cannot be
         * destroyed until libnfs has released the reference to this
         * object (queued async call with private_data pointing to
         * this object).  As soon as libnfs calls back, the object
         * will finally be destroyed.
         */
        PENDING_CLOSED,

        RELEASED,
    } state;

    const NfsClientOpenFileHandler *open_handler;
    const NfsClientReadFileHandler *read_handler;
    void *handler_ctx;

    struct async_operation operation;
};

/**
 * Wrapper for a libnfs file handle (#nfsfh).  Can feed multiple
 * #nfs_file_handle objects that are accessing the file at the same
 * time.
 *
 * After a while (#nfs_file_expiry), this object expires, and will not
 * accept any more callers; a new one will be created on demand.
 */
struct nfs_file {
    /**
     * @see NfsClient::file_list
     */
    struct list_head siblings;

    struct pool *pool;
    NfsClient *client;
    const char *path;

    enum {
        /**
         * Waiting for nfs_open_async().
         */
        PENDING_OPEN,

        /**
         * The file has been opened, and now we're waiting for
         * nfs_fstat_async().
         */
        PENDING_FSTAT,

        /**
         * The file is ready.
         */
        IDLE,

        /**
         * This object has expired.  It is no longer in
         * #NfsClient::file_map.  It will be destroyed as soon as the
         * last handle has been closed.
         */
        EXPIRED,

        RELEASED,
    } state;

    /**
     * An unordered list of #nfs_file_handle objects.
     */
    struct list_head handles;

    /**
     * Keep track of active handles.  A #nfs_file_handle is "inactive"
     * when the caller has lost interest in the object (aborted or
     * closed).
     */
    unsigned n_active_handles;

    struct nfsfh *nfsfh;

    struct stat stat;

    /**
     * Expire this object after #nfs_file_expiry.  This is only used
     * in state #IDLE.
     */
    struct event expire_event;
};

struct NfsClient {
    struct pool *pool;

    const NfsClientHandler *handler;
    void *handler_ctx;

    struct nfs_context *context;

    /**
     * libnfs I/O events.
     */
    struct event event;

    /**
     * Track mount timeout (#nfs_client_mount_timeout) and idle
     * timeout (#nfs_client_idle_timeout).
     */
    struct event timeout_event;

    /**
     * An unordered list of all #nfs_file objects.  This includes all
     * file handles that may have expired already.
     */
    struct list_head file_list;

    /**
     * Map path names to #nfs_file.  This excludes expired files.
     */
    struct hashmap *file_map;

    /**
     * Keep track of active files.  If this drops to zero, the idle
     * timer starts, and the connection is about to be closed.
     */
    unsigned n_active_files;

    /**
     * Hook that allows the caller to abort the mount operation.
     */
    struct async_operation mount_operation;

    GError *postponed_mount_error;

    /**
     * True when nfs_service() is being called.  During that,
     * nfs_client_free() is postponed, or libnfs will crash.  See
     * #postponed_destroy.
     */
    bool in_service;

    /**
     * True when nfs_client_event_callback() is being called.  During
     * that, event updates are omitted.
     */
    bool in_event;

    /**
     * True when nfs_client_free() has been called while #in_service
     * was true.
     */
    bool postponed_destroy;

    bool mount_finished;
};

static const struct timeval nfs_client_mount_timeout = {
    .tv_sec = 10,
};

static const struct timeval nfs_client_idle_timeout = {
    .tv_sec = 300,
};

static const struct timeval nfs_file_expiry = {
    .tv_sec = 60,
};

static GError *
nfs_client_new_error(int status, struct nfs_context *nfs, void *data,
                     const char *msg)
{
    assert(msg != nullptr);
    assert(status < 0);

    const char *msg2 = (const char *)data;
    if (data == nullptr || *(const char *)data == 0) {
        msg2 = nfs_get_error(nfs);
        if (msg2 == nullptr)
            msg2 = g_strerror(-status);
    }

    return g_error_new(nfs_client_quark(), -status, "%s: %s", msg, msg2);
}

static int
libnfs_to_libevent(int i)
{
    int o = 0;

    if (i & POLLIN)
        o |= EV_READ;

    if (i & POLLOUT)
        o |= EV_WRITE;

    return o;
}

static int
libevent_to_libnfs(int i)
{
    int o = 0;

    if (i & EV_READ)
        o |= POLLIN;

    if (i & EV_WRITE)
        o |= POLLOUT;

    return o;
}

/**
 * Is the object ready for reading?
 */
static bool
nfs_file_is_ready(const struct nfs_file *file)
{
    switch (file->state) {
    case nfs_file::PENDING_OPEN:
    case nfs_file::PENDING_FSTAT:
        return false;

    case nfs_file::IDLE:
    case nfs_file::EXPIRED:
        return true;

    case nfs_file::RELEASED:
        assert(false);
        gcc_unreachable();
    }

    gcc_unreachable();
}

/**
 * Make the #nfs_file "inactive".  It must be active prior to calling
 * this function.
 */
static void
nfs_file_deactivate(struct nfs_file *file)
{
    NfsClient *const client = file->client;

    assert(client->n_active_files > 0);
    --client->n_active_files;

    if (client->n_active_files == 0)
        /* the last file was deactivated: watch for idle timeout */
        evtimer_add(&client->timeout_event, &nfs_client_idle_timeout);
}

/**
 * Release an "inactive" file.  Must have called nfs_file_deactivate()
 * prior to this.
 */
static void
nfs_file_release(NfsClient *client, struct nfs_file *file)
{
    if (file->state == nfs_file::IDLE)
        evtimer_del(&file->expire_event);

    if (file->state != nfs_file::EXPIRED)
        hashmap_remove_existing(client->file_map, file->path, file);

    file->state = nfs_file::RELEASED;

    list_remove(&file->siblings);
    pool_unref(file->pool);
}

/**
 * Mark this object "inactive".  Call nfs_file_handle_release() after
 * all references by libnfs have been cleared.
 */
static void
nfs_file_handle_deactivate(NfsFileHandle *handle)
{
    struct nfs_file *const file = handle->file;

    assert(file->n_active_handles > 0);
    --file->n_active_handles;

    if (file->n_active_handles == 0)
        nfs_file_deactivate(file);
}

/**
 * Release an "inactive" handle.  Must have called
 * nfs_file_handle_deactivate() prior to this.
 */
static void
nfs_file_handle_release(NfsFileHandle *handle)
{
    assert(handle->state == NfsFileHandle::WAITING ||
           handle->state == NfsFileHandle::IDLE);

    struct nfs_file *const file = handle->file;
    assert(!list_empty(&file->handles));

    handle->state = NfsFileHandle::RELEASED;

    list_remove(&handle->siblings);
    pool_unref(handle->pool);

    if (list_empty(&file->handles) && file->state == nfs_file::EXPIRED)
        nfs_file_release(file->client, file);
}

static void
nfs_file_handle_abort(NfsFileHandle *handle, GError *error)
{
    nfs_file_handle_deactivate(handle);

    handle->open_handler->error(error, handle->handler_ctx);

    pool_unref(handle->caller_pool);
    pool_unref(handle->pool);
}

static void
nfs_file_abort_handles(struct nfs_file *file, GError *error)
{
    struct list_head *const head = &file->handles;
    while (!list_empty(head)) {
        NfsFileHandle *handle =
            (NfsFileHandle *)head->next;
        assert(handle->file == file);
        list_remove(&handle->siblings);

        nfs_file_handle_abort(handle, g_error_copy(error));
    }

    assert(file->n_active_handles == 0);
}

/**
 * Opening this file has failed.  Remove it from the client and notify
 * all waiting #http_response_handler instances.
 */
static void
nfs_client_abort_file(NfsClient *client, struct nfs_file *file,
                      GError *error)
{
    nfs_file_abort_handles(file, error);
    nfs_file_release(client, file);
}

static void
nfs_client_destroy_context(NfsClient *client)
{
    assert(client != nullptr);
    assert(client->context != nullptr);
    assert(!client->in_service);

    event_del(&client->event);
    nfs_destroy_context(client->context);
    client->context = nullptr;
}

/**
 * Mounting has failed.  Destroy the #nfs_client object and report the
 * error to the handler.
 */
static void
nfs_client_mount_error(NfsClient *client, GError *error)
{
    assert(client != nullptr);
    assert(client->context != nullptr);
    assert(!client->in_service);

    evtimer_del(&client->timeout_event);

    nfs_client_destroy_context(client);

    client->handler->mount_error(error, client->handler_ctx);
    pool_unref(client->pool);
}

static void
nfs_client_cleanup_files(NfsClient *client)
{
    for (struct nfs_file *file = (struct nfs_file *)client->file_list.next;
         &file->siblings != &client->file_list;) {
        struct nfs_file *next = (struct nfs_file *)file->siblings.next;

        if (list_empty(&file->handles)) {
            assert(file->n_active_handles == 0);

            nfs_file_release(client, file);
        }

        file = next;
    }
}

static void
nfs_client_abort_all_files(NfsClient *client, GError *error)
{
    struct list_head *const head = &client->file_list;
    while (!list_empty(head)) {
        struct nfs_file *file = (struct nfs_file *)head->next;

        nfs_client_abort_file(client, file, error);
    }
}

static void
nfs_client_error(NfsClient *client, GError *error)
{
    if (client->mount_finished) {
        evtimer_del(&client->timeout_event);

        nfs_client_abort_all_files(client, error);

        nfs_client_destroy_context(client);
        client->handler->closed(error, client->handler_ctx);
        pool_unref(client->pool);
    } else {
        nfs_client_mount_error(client, error);
    }
}

static void
nfs_client_event_callback(int fd, short event, void *ctx);

static void
nfs_client_add_event(NfsClient *client)
{
    event_set(&client->event, nfs_get_fd(client->context),
              libnfs_to_libevent(nfs_which_events(client->context)),
              nfs_client_event_callback, client);
    event_add(&client->event, nullptr);
}

static void
nfs_client_update_event(NfsClient *client)
{
    if (client->in_event)
        return;

    event_del(&client->event);
    nfs_client_add_event(client);
}

static void
nfs_read_cb(int status, struct nfs_context *nfs,
            void *data, void *private_data)
{
    NfsFileHandle *const handle =
        (NfsFileHandle *)private_data;
    assert(handle->state == NfsFileHandle::PENDING ||
           handle->state == NfsFileHandle::PENDING_CLOSED);

    const bool closed = handle->state == NfsFileHandle::PENDING_CLOSED;
    handle->state = NfsFileHandle::IDLE;

    if (closed) {
        nfs_file_handle_release(handle);
        return;
    }

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_pread_async() failed");
        handle->read_handler->error(error, handle->handler_ctx);
        return;
    }

    handle->read_handler->data(data, status, handle->handler_ctx);
}

/*
 * misc
 *
 */

static void
nfs_file_continue(struct nfs_file *file)
{
    assert(nfs_file_is_ready(file));

    struct list_head tmp_head;
    list_replace(&file->handles, &tmp_head);
    list_init(&file->handles);

    while (!list_empty(&tmp_head)) {
        NfsFileHandle *handle =
            (NfsFileHandle *)tmp_head.next;
        assert(handle->file == file);
        assert(handle->state == NfsFileHandle::WAITING);

        list_remove(&handle->siblings);
        list_add(&handle->siblings, &file->handles);

        handle->state = NfsFileHandle::IDLE;

        struct pool *const caller_pool = handle->caller_pool;

        handle->open_handler->ready(handle, &file->stat,
                                    handle->handler_ctx);
        pool_unref(caller_pool);
    }
}

/*
 * async operation
 *
 */

static NfsFileHandle *
operation_to_nfs_file_handle(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &NfsFileHandle::operation);
}

static void
nfs_file_open_operation_abort(struct async_operation *ao)
{
    NfsFileHandle *const handle = operation_to_nfs_file_handle(ao);

    pool_unref(handle->caller_pool);

    nfs_file_handle_deactivate(handle);
    nfs_file_handle_release(handle);
}

static const struct async_operation_class nfs_file_open_operation = {
    .abort = nfs_file_open_operation_abort,
};

/*
 * async operation
 *
 */

static NfsClient *
operation_to_nfs_client(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &NfsClient::mount_operation);
}

static void
nfs_client_mount_abort(struct async_operation *ao)
{
    NfsClient *const client = operation_to_nfs_client(ao);
    assert(client->context != nullptr);
    assert(!client->mount_finished);
    assert(!client->in_service);

    evtimer_del(&client->timeout_event);

    nfs_client_destroy_context(client);

    pool_unref(client->pool);
}

static const struct async_operation_class nfs_client_mount_operation = {
    .abort = nfs_client_mount_abort,
};

/*
 * libevent callback
 *
 */

static void
nfs_file_expire_callback(gcc_unused int fd, gcc_unused short event,
                         void *ctx)
{
    struct nfs_file *file = (struct nfs_file *)ctx;
    NfsClient *const client = file->client;

    assert(file->state == nfs_file::IDLE);

    if (list_empty(&file->handles)) {
        assert(file->n_active_handles == 0);

        nfs_file_release(client, file);
    } else {
        file->state = nfs_file::EXPIRED;
        hashmap_remove_existing(client->file_map, file->path, file);
    }

    pool_commit();
}

static void
nfs_client_event_callback(gcc_unused int fd, short event, void *ctx)
{
    NfsClient *client = (NfsClient *)ctx;
    assert(client->context != nullptr);

    struct pool *const pool = client->pool;
    pool_ref(pool);

    const bool was_mounted = client->mount_finished;

    assert(!client->in_event);
    client->in_event = true;

    assert(!client->in_service);
    client->in_service = true;
    client->postponed_destroy = false;

    int result = nfs_service(client->context, libevent_to_libnfs(event));

    assert(client->context != nullptr);
    assert(client->in_service);
    client->in_service = false;

    if (client->postponed_destroy) {
        /* somebody has called nfs_client_free() while we were inside
           nfs_service() */
        nfs_client_destroy_context(client);
        nfs_client_cleanup_files(client);
    } else if (!was_mounted && client->mount_finished) {
        if (client->postponed_mount_error != nullptr)
            nfs_client_mount_error(client, client->postponed_mount_error);
        else if (result == 0)
            client->handler->ready(client, client->handler_ctx);
    } else if (result < 0) {
        /* the connection has failed */
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "NFS connection has failed: %s",
                                    nfs_get_error(client->context));
        nfs_client_error(client, error);
    }

    assert(client->in_event);
    client->in_event = false;

    if (client->context != nullptr) {
        if (!was_mounted)
            /* until the mount is finished, the NFS client may use
               various sockets, therefore make sure the close-on-exec
               flag is set on all of them */
            fd_set_cloexec(nfs_get_fd(client->context), true);

        nfs_client_add_event(client);
    }

    pool_unref(pool);
    pool_commit();
}

static void
nfs_client_timeout_callback(gcc_unused int fd, gcc_unused short event,
                            void *ctx)
{
    NfsClient *client = (NfsClient *)ctx;
    assert(client->context != nullptr);

    if (client->mount_finished) {
        assert(client->n_active_files == 0);

        nfs_client_destroy_context(client);
        GError *error = g_error_new_literal(nfs_client_quark(), 0,
                                            "Idle timeout");
        client->handler->closed(error, client->handler_ctx);
        pool_unref(client->pool);
    } else {
        client->mount_finished = true;

        GError *error = g_error_new_literal(nfs_client_quark(), 0,
                                            "Mount timeout");
        nfs_client_mount_error(client, error);
    }

    pool_commit();
}

/*
 * libnfs callbacks
 *
 */

static void
nfs_mount_cb(int status, struct nfs_context *nfs, void *data,
             void *private_data)
{
    NfsClient *client = (NfsClient *)private_data;

    client->mount_finished = true;

    if (status < 0) {
        client->postponed_mount_error =
            nfs_client_new_error(status, nfs, data,
                                 "nfs_mount_async() failed");
        return;
    }

    client->postponed_mount_error = nullptr;

    client->file_map = hashmap_new(client->pool, 59);
    list_init(&client->file_list);
    client->n_active_files = 0;
}

static void
nfs_fstat_cb(int status, gcc_unused struct nfs_context *nfs,
             void *data, void *private_data)
{
    struct nfs_file *const file = (struct nfs_file *)private_data;
    assert(file->state == nfs_file::PENDING_FSTAT);

    NfsClient *const client = file->client;

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_fstat_async() failed");
        nfs_client_abort_file(client, file, error);
        g_error_free(error);
        return;
    }

    const struct stat *st = (const struct stat *)data;

    if (!S_ISREG(st->st_mode)) {
        GError *error = g_error_new_literal(errno_quark(), ENOENT,
                                            "Not a regular file");
        nfs_client_abort_file(client, file, error);
        g_error_free(error);
        return;
    }

    file->stat = *st;
    file->state = nfs_file::IDLE;
    evtimer_set(&file->expire_event, nfs_file_expire_callback, file);
    evtimer_add(&file->expire_event, &nfs_file_expiry);

    nfs_file_continue(file);
}

static void
nfs_open_cb(int status, gcc_unused struct nfs_context *nfs,
            void *data, void *private_data)
{
    struct nfs_file *const file = (struct nfs_file *)private_data;
    assert(file->state == nfs_file::PENDING_OPEN);

    NfsClient *const client = file->client;

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_open_async() failed");
        nfs_client_abort_file(client, file, error);
        g_error_free(error);
        return;
    }

    file->nfsfh = (struct nfsfh *)data;
    file->state = nfs_file::PENDING_FSTAT;

    int result = nfs_fstat_async(file->client->context, file->nfsfh,
                                 nfs_fstat_cb, file);
    if (result != 0) {
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "nfs_fstat_async() failed: %s",
                                    nfs_get_error(client->context));
        nfs_client_abort_file(client, file, error);
        g_error_free(error);
        return;
    }
}

/*
 * constructor
 *
 */

void
nfs_client_new(struct pool *pool, const char *server, const char *root,
               const NfsClientHandler *handler, void *ctx,
               struct async_operation_ref *async_ref)
{
    assert(pool != nullptr);
    assert(server != nullptr);
    assert(root != nullptr);
    assert(handler != nullptr);
    assert(handler->ready != nullptr);
    assert(handler->mount_error != nullptr);
    assert(handler->closed != nullptr);

    auto client = NewFromPool<NfsClient>(*pool);
    client->pool = pool;

    client->context = nfs_init_context();
    if (client->context == nullptr) {
        handler->mount_error(g_error_new(nfs_client_quark(), 0,
                                         "nfs_init_context() failed"), ctx);
        return;
    }

    pool_ref(pool);

    client->handler = handler;
    client->handler_ctx = ctx;
    client->mount_finished = false;
    client->in_service = false;
    client->in_event = false;

    if (nfs_mount_async(client->context, server, root,
                        nfs_mount_cb, client) != 0) {
        GError *error =
            g_error_new(nfs_client_quark(), 0,
                        "nfs_mount_async() failed: %s",
                        nfs_get_error(client->context));

        nfs_client_mount_error(client, error);
        return;
    }

    fd_set_cloexec(nfs_get_fd(client->context), true);

    nfs_client_add_event(client);

    client->mount_operation.Init(nfs_client_mount_operation);
    async_ref->Set(client->mount_operation);

    evtimer_set(&client->timeout_event, nfs_client_timeout_callback, client);
    evtimer_add(&client->timeout_event, &nfs_client_mount_timeout);
}

void
nfs_client_free(NfsClient *client)
{
    assert(client != nullptr);
    assert(client->n_active_files == 0);

    evtimer_del(&client->timeout_event);

    if (client->in_service) {
        client->postponed_destroy = true;
    } else {
        nfs_client_destroy_context(client);
        nfs_client_cleanup_files(client);
    }

    pool_unref(client->pool);
}

void
nfs_client_open_file(NfsClient *client, struct pool *caller_pool,
                     const char *path,
                     const NfsClientOpenFileHandler *handler,
                     void *ctx,
                     struct async_operation_ref *async_ref)
{
    assert(client->context != nullptr);

    struct nfs_file *file = (struct nfs_file *)
        hashmap_get(client->file_map, path);
    if (file == nullptr) {
        struct pool *f_pool = pool_new_libc(client->pool, "nfs_file");
        file = NewFromPool<struct nfs_file>(*f_pool);
        file->pool = f_pool;
        file->client = client;
        file->path = p_strdup(f_pool, path);
        file->state = nfs_file::PENDING_OPEN;
        list_init(&file->handles);
        file->n_active_handles = 0;

        hashmap_add(client->file_map, file->path, file);
        list_add(&file->siblings, &client->file_list);

        if (nfs_open_async(client->context, file->path, O_RDONLY,
                           nfs_open_cb, file) != 0) {
            list_remove(&file->siblings);
            pool_unref(file->pool);

            GError *error = g_error_new(nfs_client_quark(), 0,
                                        "nfs_open_async() failed: %s",
                                        nfs_get_error(client->context));
            handler->error(error, ctx);
            return;
        }
    }

    struct pool *r_pool = pool_new_libc(file->pool, "nfs_file_handle");

    auto handle = NewFromPool<NfsFileHandle>(*r_pool);
    handle->file = file;
    handle->pool = r_pool;
    handle->caller_pool = caller_pool;

    list_add(&handle->siblings, &file->handles);
    ++file->n_active_handles;
    if (file->n_active_handles == 1) {
        if (client->n_active_files == 0)
            /* cancel the idle timeout */
            evtimer_del(&client->timeout_event);

        ++client->n_active_files;
    }

    nfs_client_update_event(client);

    if (nfs_file_is_ready(file)) {
        handle->state = NfsFileHandle::IDLE;

        handler->ready(handle, &file->stat, ctx);
    } else {
        handle->state = NfsFileHandle::WAITING;
        handle->open_handler = handler;
        handle->handler_ctx = ctx;

        handle->operation.Init(nfs_file_open_operation);
        async_ref->Set(handle->operation);

        pool_ref(caller_pool);
    }
}

void
nfs_client_close_file(NfsFileHandle *handle)
{
    assert(nfs_file_is_ready(handle->file));

    nfs_file_handle_deactivate(handle);

    switch (handle->state) {
    case NfsFileHandle::WAITING:
    case NfsFileHandle::PENDING_CLOSED:
    case NfsFileHandle::RELEASED:
        assert(false);
        gcc_unreachable();

    case NfsFileHandle::IDLE:
        nfs_file_handle_release(handle);
        break;

    case NfsFileHandle::PENDING:
        /* a request is still pending; postpone the close until libnfs
           has called back */
        handle->state = NfsFileHandle::PENDING_CLOSED;
        break;
    }
}

void
nfs_client_read_file(NfsFileHandle *handle,
                     uint64_t offset, size_t length,
                     const NfsClientReadFileHandler *handler,
                     void *ctx)
{
    struct nfs_file *const file = handle->file;
    NfsClient *const client = file->client;

    assert(handle->state == NfsFileHandle::IDLE);

    if (nfs_pread_async(client->context, file->nfsfh,
                        offset, length,
                        nfs_read_cb, handle) != 0) {
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "nfs_fstat_async() failed: %s",
                                    nfs_get_error(client->context));
        handler->error(error, ctx);
        return;
    }

    handle->read_handler = handler;
    handle->handler_ctx = ctx;
    handle->state = NfsFileHandle::PENDING;

    nfs_client_update_event(client);
}
