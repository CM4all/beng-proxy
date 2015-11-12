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

struct NfsFile;

/**
 * A handle that is passed to the caller.  Each file can have multiple
 * public "handles", one for each caller.  That way, only one #nfsfh
 * (inside #NfsFile) is needed.
 */
struct NfsFileHandle {
    /**
     * @see NfsFile::handles
     */
    struct list_head siblings;

    NfsFile *file;

    /**
     * A child of #NfsFile::pool.
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

    /**
     * Mark this object "inactive".  Call Release() after all
     * references by libnfs have been cleared.
     */
    void Deactivate();

    /**
     * Release an "inactive" handle.  Must have called Deactivate()
     * prior to this.
     */
    void Release();

    void Abort(GError *error);

    void Close();
    void Read(uint64_t offset, size_t length,
              const NfsClientReadFileHandler &handler, void *ctx);
};

/**
 * Wrapper for a libnfs file handle (#nfsfh).  Can feed multiple
 * #NfsFileHandle objects that are accessing the file at the same
 * time.
 *
 * After a while (#nfs_file_expiry), this object expires, and will not
 * accept any more callers; a new one will be created on demand.
 */
struct NfsFile {
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
     * An unordered list of #NfsFileHandle objects.
     */
    struct list_head handles;

    /**
     * Keep track of active handles.  A #NfsFileHandle is "inactive"
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

    /**
     * Is the object ready for reading?
     */
    gcc_pure
    bool IsReady() const;

    /**
     * Make the #NfsFile "inactive".  It must be active prior to
     * calling this function.
     */
    void Deactivate();

    /**
     * Release an "inactive" file.  Must have called Deactivate()
     * prior to this.
     */
    void Release();

    void AbortHandles(GError *error);

    /**
     * Opening this file has failed.  Remove it from the client and notify
     * all waiting #http_response_handler instances.
     */
    void Abort(GError *error);

    void Continue();
};

struct NfsClient {
    struct pool *pool;

    NfsClientHandler *handler;

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
     * An unordered list of all #NfsFile objects.  This includes all
     * file handles that may have expired already.
     */
    struct list_head file_list;

    /**
     * Map path names to #NfsFile.  This excludes expired files.
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

    void DestroyContext();

    /**
     * Mounting has failed.  Destroy the #NfsClientHandler object and
     * report the error to the handler.
     */
    void MountError(GError *error);

    void CleanupFiles();
    void AbortAllFiles(GError *error);
    void Error(GError *error);

    void AddEvent();
    void UpdateEvent();

    void OpenFile(struct pool &caller_pool,
                  const char *path,
                  const NfsClientOpenFileHandler &handler, void *ctx,
                  struct async_operation_ref &async_ref);
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

inline bool
NfsFile::IsReady() const
{
    switch (state) {
    case PENDING_OPEN:
    case PENDING_FSTAT:
        return false;

    case IDLE:
    case EXPIRED:
        return true;

    case RELEASED:
        assert(false);
        gcc_unreachable();
    }

    gcc_unreachable();
}

inline void
NfsFile::Deactivate()
{
    assert(client->n_active_files > 0);
    --client->n_active_files;

    if (client->n_active_files == 0)
        /* the last file was deactivated: watch for idle timeout */
        evtimer_add(&client->timeout_event, &nfs_client_idle_timeout);
}

void
NfsFile::Release()
{
    if (state == IDLE)
        evtimer_del(&expire_event);

    if (state != EXPIRED)
        hashmap_remove_existing(client->file_map, path, this);

    state = RELEASED;

    list_remove(&siblings);
    pool_unref(pool);
}

void
NfsFileHandle::Deactivate()
{
    assert(file->n_active_handles > 0);
    --file->n_active_handles;

    if (file->n_active_handles == 0)
        file->Deactivate();
}

void
NfsFileHandle::Release()
{
    assert(state == WAITING || state == IDLE);

    NfsFile *const _file = file;
    assert(!list_empty(&_file->handles));

    state = RELEASED;

    list_remove(&siblings);
    pool_unref(pool);

    if (list_empty(&_file->handles) && _file->state == NfsFile::EXPIRED)
        _file->Release();
}

void
NfsFileHandle::Abort(GError *error)
{
    Deactivate();

    open_handler->error(error, handler_ctx);

    pool_unref(caller_pool);
    pool_unref(pool);
}

inline void
NfsFile::AbortHandles(GError *error)
{
    struct list_head *const head = &handles;
    while (!list_empty(head)) {
        NfsFileHandle *handle =
            (NfsFileHandle *)head->next;
        assert(handle->file == this);
        list_remove(&handle->siblings);

        handle->Abort(g_error_copy(error));
    }

    assert(n_active_handles == 0);
}

void
NfsFile::Abort(GError *error)
{
    AbortHandles(error);
    Release();
}

void
NfsClient::DestroyContext()
{
    assert(context != nullptr);
    assert(!in_service);

    event_del(&event);
    nfs_destroy_context(context);
    context = nullptr;
}

void
NfsClient::MountError(GError *error)
{
    assert(context != nullptr);
    assert(!in_service);

    evtimer_del(&timeout_event);

    DestroyContext();

    handler->OnNfsMountError(error);
    pool_unref(pool);
}

void
NfsClient::CleanupFiles()
{
    for (NfsFile *file = (NfsFile *)file_list.next;
         &file->siblings != &file_list;) {
        NfsFile *next = (NfsFile *)file->siblings.next;

        if (list_empty(&file->handles)) {
            assert(file->n_active_handles == 0);

            file->Release();
        }

        file = next;
    }
}

inline void
NfsClient::AbortAllFiles(GError *error)
{
    struct list_head *const head = &file_list;
    while (!list_empty(head)) {
        NfsFile *file = (NfsFile *)head->next;

        file->Abort(error);
    }
}

inline void
NfsClient::Error(GError *error)
{
    if (mount_finished) {
        evtimer_del(&timeout_event);

        AbortAllFiles(error);

        DestroyContext();
        handler->OnNfsClientClosed(error);
        pool_unref(pool);
    } else {
        MountError(error);
    }
}

static void
nfs_client_event_callback(int fd, short event, void *ctx);

void
NfsClient::AddEvent()
{
    event_set(&event, nfs_get_fd(context),
              libnfs_to_libevent(nfs_which_events(context)),
              nfs_client_event_callback, this);
    event_add(&event, nullptr);
}

void
NfsClient::UpdateEvent()
{
    if (in_event)
        return;

    event_del(&event);
    AddEvent();
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
        handle->Release();
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

inline void
NfsFile::Continue()
{
    assert(IsReady());

    struct list_head tmp_head;
    list_replace(&handles, &tmp_head);
    list_init(&handles);

    while (!list_empty(&tmp_head)) {
        NfsFileHandle *handle =
            (NfsFileHandle *)tmp_head.next;
        assert(handle->file == this);
        assert(handle->state == NfsFileHandle::WAITING);

        list_remove(&handle->siblings);
        list_add(&handle->siblings, &handles);

        handle->state = NfsFileHandle::IDLE;

        struct pool *const caller_pool = handle->caller_pool;

        handle->open_handler->ready(handle, &stat,
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

    handle->Deactivate();
    handle->Release();
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

    client->DestroyContext();

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
    NfsFile *file = (NfsFile *)ctx;
    NfsClient *const client = file->client;

    assert(file->state == NfsFile::IDLE);

    if (list_empty(&file->handles)) {
        assert(file->n_active_handles == 0);

        file->Release();
    } else {
        file->state = NfsFile::EXPIRED;
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
        client->DestroyContext();
        client->CleanupFiles();
    } else if (!was_mounted && client->mount_finished) {
        if (client->postponed_mount_error != nullptr)
            client->MountError(client->postponed_mount_error);
        else if (result == 0)
            client->handler->OnNfsClientReady(*client);
    } else if (result < 0) {
        /* the connection has failed */
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "NFS connection has failed: %s",
                                    nfs_get_error(client->context));
        client->Error(error);
    }

    assert(client->in_event);
    client->in_event = false;

    if (client->context != nullptr) {
        if (!was_mounted)
            /* until the mount is finished, the NFS client may use
               various sockets, therefore make sure the close-on-exec
               flag is set on all of them */
            fd_set_cloexec(nfs_get_fd(client->context), true);

        client->AddEvent();
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

        client->DestroyContext();
        GError *error = g_error_new_literal(nfs_client_quark(), 0,
                                            "Idle timeout");
        client->handler->OnNfsClientClosed(error);
        pool_unref(client->pool);
    } else {
        client->mount_finished = true;

        GError *error = g_error_new_literal(nfs_client_quark(), 0,
                                            "Mount timeout");
        client->MountError(error);
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
    NfsFile *const file = (NfsFile *)private_data;
    assert(file->state == NfsFile::PENDING_FSTAT);

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_fstat_async() failed");
        file->Abort(error);
        g_error_free(error);
        return;
    }

    const struct stat *st = (const struct stat *)data;

    if (!S_ISREG(st->st_mode)) {
        GError *error = g_error_new_literal(errno_quark(), ENOENT,
                                            "Not a regular file");
        file->Abort(error);
        g_error_free(error);
        return;
    }

    file->stat = *st;
    file->state = NfsFile::IDLE;
    evtimer_set(&file->expire_event, nfs_file_expire_callback, file);
    evtimer_add(&file->expire_event, &nfs_file_expiry);

    file->Continue();
}

static void
nfs_open_cb(int status, gcc_unused struct nfs_context *nfs,
            void *data, void *private_data)
{
    NfsFile *const file = (NfsFile *)private_data;
    assert(file->state == NfsFile::PENDING_OPEN);

    NfsClient *const client = file->client;

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_open_async() failed");
        file->Abort(error);
        g_error_free(error);
        return;
    }

    file->nfsfh = (struct nfsfh *)data;
    file->state = NfsFile::PENDING_FSTAT;

    int result = nfs_fstat_async(file->client->context, file->nfsfh,
                                 nfs_fstat_cb, file);
    if (result != 0) {
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "nfs_fstat_async() failed: %s",
                                    nfs_get_error(client->context));
        file->Abort(error);
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
               NfsClientHandler &handler,
               struct async_operation_ref *async_ref)
{
    assert(pool != nullptr);
    assert(server != nullptr);
    assert(root != nullptr);

    auto client = NewFromPool<NfsClient>(*pool);
    client->pool = pool;

    client->context = nfs_init_context();
    if (client->context == nullptr) {
        handler.OnNfsMountError(g_error_new(nfs_client_quark(), 0,
                                            "nfs_init_context() failed"));
        return;
    }

    pool_ref(pool);

    client->handler = &handler;
    client->mount_finished = false;
    client->in_service = false;
    client->in_event = false;

    if (nfs_mount_async(client->context, server, root,
                        nfs_mount_cb, client) != 0) {
        GError *error =
            g_error_new(nfs_client_quark(), 0,
                        "nfs_mount_async() failed: %s",
                        nfs_get_error(client->context));

        client->MountError(error);
        return;
    }

    fd_set_cloexec(nfs_get_fd(client->context), true);

    client->AddEvent();

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
        client->DestroyContext();
        client->CleanupFiles();
    }

    pool_unref(client->pool);
}

inline void
NfsClient::OpenFile(struct pool &caller_pool,
                    const char *path,
                    const NfsClientOpenFileHandler &_handler, void *ctx,
                    struct async_operation_ref &async_ref)
{
    assert(context != nullptr);

    NfsFile *file = (NfsFile *)hashmap_get(file_map, path);
    if (file == nullptr) {
        struct pool *f_pool = pool_new_libc(pool, "nfs_file");
        file = NewFromPool<NfsFile>(*f_pool);
        file->pool = f_pool;
        file->client = this;
        file->path = p_strdup(f_pool, path);
        file->state = NfsFile::PENDING_OPEN;
        list_init(&file->handles);
        file->n_active_handles = 0;

        hashmap_add(file_map, file->path, file);
        list_add(&file->siblings, &file_list);

        if (nfs_open_async(context, file->path, O_RDONLY,
                           nfs_open_cb, file) != 0) {
            list_remove(&file->siblings);
            pool_unref(file->pool);

            GError *error = g_error_new(nfs_client_quark(), 0,
                                        "nfs_open_async() failed: %s",
                                        nfs_get_error(context));
            _handler.error(error, ctx);
            return;
        }
    }

    struct pool *r_pool = pool_new_libc(file->pool, "NfsFileHandle");

    auto handle = NewFromPool<NfsFileHandle>(*r_pool);
    handle->file = file;
    handle->pool = r_pool;
    handle->caller_pool = &caller_pool;

    list_add(&handle->siblings, &file->handles);
    ++file->n_active_handles;
    if (file->n_active_handles == 1) {
        if (n_active_files == 0)
            /* cancel the idle timeout */
            evtimer_del(&timeout_event);

        ++n_active_files;
    }

    UpdateEvent();

    if (file->IsReady()) {
        handle->state = NfsFileHandle::IDLE;

        _handler.ready(handle, &file->stat, ctx);
    } else {
        handle->state = NfsFileHandle::WAITING;
        handle->open_handler = &_handler;
        handle->handler_ctx = ctx;

        handle->operation.Init(nfs_file_open_operation);
        async_ref.Set(handle->operation);

        pool_ref(&caller_pool);
    }
}

void
nfs_client_open_file(NfsClient *client, struct pool *caller_pool,
                     const char *path,
                     const NfsClientOpenFileHandler *handler,
                     void *ctx,
                     struct async_operation_ref *async_ref)
{
    client->OpenFile(*caller_pool, path, *handler, ctx, *async_ref);
}

inline void
NfsFileHandle::Close()
{
    assert(file->IsReady());

    Deactivate();

    switch (state) {
    case WAITING:
    case PENDING_CLOSED:
    case RELEASED:
        assert(false);
        gcc_unreachable();

    case IDLE:
        Release();
        break;

    case PENDING:
        /* a request is still pending; postpone the close until libnfs
           has called back */
        state = PENDING_CLOSED;
        break;
    }
}

void
nfs_client_close_file(NfsFileHandle *handle)
{
    handle->Close();
}

inline void
NfsFileHandle::Read(uint64_t offset, size_t length,
                    const NfsClientReadFileHandler &handler,
                    void *ctx)
{
    assert(state == IDLE);

    auto &client = *file->client;

    if (nfs_pread_async(client.context, file->nfsfh,
                        offset, length,
                        nfs_read_cb, this) != 0) {
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "nfs_fstat_async() failed: %s",
                                    nfs_get_error(client.context));
        handler.error(error, ctx);
        return;
    }

    read_handler = &handler;
    handler_ctx = ctx;
    state = PENDING;

    client.UpdateEvent();
}

void
nfs_client_read_file(NfsFileHandle *handle,
                     uint64_t offset, size_t length,
                     const NfsClientReadFileHandler *handler,
                     void *ctx)
{
    return handle->Read(offset, length, *handler, ctx);
}
