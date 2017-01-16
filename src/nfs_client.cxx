/*
 * Glue for libnfs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_client.hxx"
#include "pool.hxx"
#include "gerrno.h"
#include "system/fd_util.h"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"
#include "util/Cancellable.hxx"

extern "C" {
#include <nfsc/libnfs.h>
}

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

#include <iterator>

#include <string.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/stat.h>

class NfsFile;

/**
 * A handle that is passed to the caller.  Each file can have multiple
 * public "handles", one for each caller.  That way, only one #nfsfh
 * (inside #NfsFile) is needed.
 */
class NfsFileHandle final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      Cancellable {

    NfsFile &file;

    /**
     * A child of #NfsFile::pool.
     */
    struct pool &pool;

    /**
     * The pool provided by the caller.  It must be referenced until
     * the response has been delivered.
     */
    struct pool &caller_pool;

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

    NfsClientOpenFileHandler *open_handler;
    NfsClientReadFileHandler *read_handler;

public:
    NfsFileHandle(NfsFile &_file, struct pool &_pool,
                  struct pool &_caller_pool)
        :file(_file), pool(_pool), caller_pool(_caller_pool) {}

    void Continue(const struct stat &st) {
        assert(state == WAITING);
        state = IDLE;

        struct pool &cp = caller_pool;
        open_handler->OnNfsOpen(this, &st);
        pool_unref(&cp);
    }

    void Continue(NfsClientOpenFileHandler &_handler,
                  const struct stat &st) {
        assert(state == WAITING);
        state = IDLE;

        _handler.OnNfsOpen(this, &st);
    }

    void Wait(NfsClientOpenFileHandler &_handler,
              CancellablePointer &cancel_ptr) {
        state = WAITING;
        open_handler = &_handler;
        cancel_ptr = *this;

        pool_ref(&caller_pool);
    }

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
              NfsClientReadFileHandler &handler);

    void ReadCallback(int status, struct nfs_context *nfs, void *data);

    /* virtual methods from class Cancellable */
    void Cancel() override;
};

/**
 * Wrapper for a libnfs file handle (#nfsfh).  Can feed multiple
 * #NfsFileHandle objects that are accessing the file at the same
 * time.
 *
 * After a while (#nfs_file_expiry), this object expires, and will not
 * accept any more callers; a new one will be created on demand.
 */
class NfsFile
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      public boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;
    NfsClient &client;
    const char *const path;

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
    } state = PENDING_OPEN;

    /**
     * An unordered list of #NfsFileHandle objects.
     */
    boost::intrusive::list<NfsFileHandle,
                           boost::intrusive::constant_time_size<false>> handles;

    /**
     * Keep track of active handles.  A #NfsFileHandle is "inactive"
     * when the caller has lost interest in the object (aborted or
     * closed).
     */
    unsigned n_active_handles = 0;

    struct nfsfh *nfsfh;

    struct stat stat;

    /**
     * Expire this object after #nfs_file_expiry.  This is only used
     * in state #IDLE.
     */
    TimerEvent expire_event;

public:
    NfsFile(EventLoop &event_loop, struct pool &_pool,
            NfsClient &_client, const char *_path)
        :pool(_pool), client(_client),
         path(p_strdup(&pool, _path)),
         expire_event(event_loop, BIND_THIS_METHOD(ExpireCallback)) {}

    bool Open(struct nfs_context *context, GError **error_r);

    void Destroy() {
        pool_unref(&pool);
    }

    /**
     * Is the object ready for reading?
     */
    gcc_pure
    bool IsReady() const;

    bool IsExpired() const {
        return state == EXPIRED;
    }

    const struct stat &GetStat() const {
        assert(IsReady());

        return stat;
    }

    bool HasHandles() const {
        return !handles.empty();
    }

    bool HasActiveHandles() const {
        return n_active_handles > 0;
    }

    /**
     * Make the #NfsFile "inactive".  It must be active prior to
     * calling this function.
     */
    void Deactivate();

    void Unreference() {
        assert(n_active_handles > 0);
        --n_active_handles;

        if (n_active_handles == 0)
            Deactivate();
    }

    /**
     * Release an "inactive" file.  Must have called Deactivate()
     * prior to this.
     */
    void Release();

    NfsFileHandle *NewHandle(struct pool &caller_pool) {
        struct pool *r_pool = pool_new_libc(&pool, "NfsFileHandle");
        auto *handle = NewFromPool<NfsFileHandle>(*r_pool, *this, *r_pool,
                                                  caller_pool);
        handles.push_front(*handle);
        ++n_active_handles;

        return handle;
    }

    void RemoveHandle(NfsFileHandle &h) {
        assert(!handles.empty());

        handles.erase(handles.iterator_to(h));

        if (handles.empty() && state == NfsFile::EXPIRED)
            Release();
    }

    void AbortHandles(GError *error);

    /**
     * Opening this file has failed.  Remove it from the client and notify
     * all waiting #http_response_handler instances.
     */
    void Abort(GError *error);

    void Continue();

    bool ReadAsync(uint64_t offset, uint64_t count,
                   nfs_cb cb, void *private_data,
                   GError **error_r);

    void ExpireCallback();

    void FstatCallback(int status, struct nfs_context *nfs, void *data);
    void OpenCallback(int status, struct nfs_context *nfs, void *data);

    struct Compare {
        bool operator()(const NfsFile &a, const NfsFile &b) const {
            return strcmp(a.path, b.path) < 0;
        }

        bool operator()(const NfsFile &a, const char *b) const {
            return strcmp(a.path, b) < 0;
        }

        bool operator()(const char *a, const NfsFile &b) const {
            return strcmp(a, b.path) < 0;
        }
    };
};

class NfsClient final : Cancellable {
    struct pool &pool;

    NfsClientHandler &handler;

    struct nfs_context *context;

    /**
     * libnfs I/O events.
     */
    SocketEvent event;

    /**
     * Track mount timeout (#nfs_client_mount_timeout) and idle
     * timeout (#nfs_client_idle_timeout).
     */
    TimerEvent timeout_event;

    /**
     * An unordered list of all #NfsFile objects.  This includes all
     * file handles that may have expired already.
     */
    boost::intrusive::list<NfsFile,
                           boost::intrusive::constant_time_size<false>> file_list;

    /**
     * Map path names to #NfsFile.  This excludes expired files.
     */
    typedef boost::intrusive::set<NfsFile,
                                  boost::intrusive::compare<NfsFile::Compare>,
                                  boost::intrusive::constant_time_size<false>> FileMap;
    FileMap file_map;

    /**
     * Keep track of active files.  If this drops to zero, the idle
     * timer starts, and the connection is about to be closed.
     */
    unsigned n_active_files;

    GError *postponed_mount_error;

    /**
     * True when nfs_service() is being called.  During that,
     * nfs_client_free() is postponed, or libnfs will crash.  See
     * #postponed_destroy.
     */
    bool in_service = false;

    /**
     * True when SocketEventCallback() is being called.  During that,
     * event updates are omitted.
     */
    bool in_event = false;

    /**
     * True when nfs_client_free() has been called while #in_service
     * was true.
     */
    bool postponed_destroy;

    bool mount_finished = false;

public:
    NfsClient(EventLoop &event_loop, struct pool &_pool,
              NfsClientHandler &_handler,
              struct nfs_context &_context)
        :pool(_pool),
         handler(_handler), context(&_context),
         event(event_loop, BIND_THIS_METHOD(SocketEventCallback)),
         timeout_event(event_loop, BIND_THIS_METHOD(TimeoutCallback)) {
        pool_ref(&pool);
    }

    EventLoop &GetEventLoop() {
        return event.GetEventLoop();
    }

    bool Mount(const char *server, const char *exportname,
               CancellablePointer &cancel_ptr);

    void DestroyContext();
    void Destroy();

    /**
     * Mounting has failed.  Destroy the #NfsClientHandler object and
     * report the error to the handler.
     */
    void MountError(GError *error);
    void MountCallback(int status, struct nfs_context *nfs, void *data);

    void CleanupFiles();
    void AbortAllFiles(GError *error);
    void Error(GError *error);

    void AddEvent();
    void UpdateEvent();
    void SocketEventCallback(short events);
    void TimeoutCallback();

    void OpenFile(struct pool &caller_pool,
                  const char *path,
                  NfsClientOpenFileHandler &handler,
                  CancellablePointer &cancel_ptr);

    void DeactivateFile();

    void ExpireFile(NfsFile &file) {
        file_map.erase(file_map.iterator_to(file));
    }

    void RemoveFile(NfsFile &file) {
        if (!file.IsExpired())
            file_map.erase(file_map.iterator_to(file));

        file_list.erase(file_list.iterator_to(file));
    }

    bool MountAsync(const char *server, const char *exportname,
                    nfs_cb cb, void *private_data,
                    GError **error_r);

    bool ReadAsync(struct nfsfh *nfsfh, uint64_t offset, uint64_t count,
                   nfs_cb cb, void *private_data, GError **error_r);

    bool FstatAsync(struct nfsfh *nfsfh, nfs_cb cb, void *private_data,
                    GError **error_r);

    /* virtual methods from class Cancellable */
    void Cancel() override;
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
NfsClient::DeactivateFile()
{
    assert(n_active_files > 0);
    --n_active_files;

    if (n_active_files == 0)
        /* the last file was deactivated: watch for idle timeout */
        timeout_event.Add(nfs_client_idle_timeout);
}

inline void
NfsFile::Deactivate()
{
    client.DeactivateFile();
}

void
NfsFile::Release()
{
    assert(handles.empty());
    assert(n_active_handles == 0);

    if (state == IDLE)
        expire_event.Cancel();

    client.RemoveFile(*this);

    state = RELEASED;

    Destroy();
}

void
NfsFileHandle::Deactivate()
{
    file.Unreference();
}

void
NfsFileHandle::Release()
{
    assert(state == WAITING || state == IDLE);

    NfsFile &_file = file;

    state = RELEASED;

    _file.RemoveHandle(*this);
    pool_unref(&pool);
}

void
NfsFileHandle::Abort(GError *error)
{
    Deactivate();

    open_handler->OnNfsOpenError(error);

    pool_unref(&caller_pool);
    pool_unref(&pool);
}

inline void
NfsFile::AbortHandles(GError *error)
{
    handles.clear_and_dispose([this, error](NfsFileHandle *handle){
            handle->Abort(g_error_copy(error));
        });

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

    event.Delete();
    nfs_destroy_context(context);
    context = nullptr;
}

void
NfsClient::MountError(GError *error)
{
    assert(context != nullptr);
    assert(!in_service);

    timeout_event.Cancel();

    DestroyContext();

    handler.OnNfsMountError(error);
    pool_unref(&pool);
}

void
NfsClient::CleanupFiles()
{
    for (auto file = file_list.begin(), end = file_list.end(); file != end;) {
        auto next = std::next(file);

        if (!file->HasHandles())
            file->Release();

        file = next;
    }
}

inline void
NfsClient::AbortAllFiles(GError *error)
{
    file_list.clear_and_dispose([error](NfsFile *file){
            file->Abort(g_error_copy(error));
        });
}

inline void
NfsClient::Error(GError *error)
{
    if (mount_finished) {
        timeout_event.Cancel();

        AbortAllFiles(error);

        DestroyContext();
        handler.OnNfsClientClosed(error);
        pool_unref(&pool);
    } else {
        MountError(error);
    }
}

void
NfsClient::AddEvent()
{
    event.Set(nfs_get_fd(context),
              libnfs_to_libevent(nfs_which_events(context)));
    event.Add();
}

void
NfsClient::UpdateEvent()
{
    if (in_event)
        return;

    event.Delete();
    AddEvent();
}

inline void
NfsFileHandle::ReadCallback(int status, struct nfs_context *nfs, void *data)
{
    assert(state == PENDING || state == PENDING_CLOSED);

    const bool closed = state == PENDING_CLOSED;
    state = IDLE;

    if (closed) {
        Release();
        return;
    }

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_pread_async() failed");
        read_handler->OnNfsReadError(error);
        return;
    }

    read_handler->OnNfsRead(data, status);
}

static void
nfs_read_cb(int status, struct nfs_context *nfs,
            void *data, void *private_data)
{
    auto &handle = *(NfsFileHandle *)private_data;
    handle.ReadCallback(status, nfs, data);
}

/*
 * misc
 *
 */

inline void
NfsFile::Continue()
{
    assert(IsReady());

    auto tmp = std::move(handles);
    handles.clear();

    tmp.clear_and_dispose([this](NfsFileHandle *handle){
            handles.push_front(*handle);

            handle->Continue(stat);
        });
}

bool
NfsFile::ReadAsync(uint64_t offset, uint64_t count,
                   nfs_cb cb, void *private_data,
                   GError **error_r)
{
    return client.ReadAsync(nfsfh, offset, count,
                            cb, private_data, error_r);
}

/*
 * async operation
 *
 */

void
NfsFileHandle::Cancel()
{
    pool_unref(&caller_pool);

    Deactivate();
    Release();
}

/*
 * async operation
 *
 */

void
NfsClient::Cancel()
{
    assert(context != nullptr);
    assert(!mount_finished);
    assert(!in_service);

    timeout_event.Cancel();

    DestroyContext();

    pool_unref(&pool);
}

/*
 * libevent callback
 *
 */

void
NfsFile::ExpireCallback()
{
    assert(state == IDLE);

    if (handles.empty()) {
        assert(n_active_handles == 0);

        Release();
    } else {
        state = EXPIRED;
        client.ExpireFile(*this);
    }

    pool_commit();
}

inline void
NfsClient::SocketEventCallback(short events)
{
    assert(context != nullptr);

    const ScopePoolRef ref(pool TRACE_ARGS);

    const bool was_mounted = mount_finished;

    assert(!in_event);
    in_event = true;

    assert(!in_service);
    in_service = true;
    postponed_destroy = false;

    int result = nfs_service(context, libevent_to_libnfs(events));

    assert(context != nullptr);
    assert(in_service);
    in_service = false;

    if (postponed_destroy) {
        /* somebody has called nfs_client_free() while we were inside
           nfs_service() */
        DestroyContext();
        CleanupFiles();
    } else if (!was_mounted && mount_finished) {
        if (postponed_mount_error != nullptr)
            MountError(postponed_mount_error);
        else if (result == 0)
            handler.OnNfsClientReady(*this);
    } else if (result < 0) {
        /* the connection has failed */
        GError *error = g_error_new(nfs_client_quark(), 0,
                                    "NFS connection has failed: %s",
                                    nfs_get_error(context));
        Error(error);
    }

    assert(in_event);
    in_event = false;

    if (context != nullptr) {
        if (!was_mounted)
            /* until the mount is finished, the NFS client may use
               various sockets, therefore make sure the close-on-exec
               flag is set on all of them */
            fd_set_cloexec(nfs_get_fd(context), true);

        AddEvent();
    }
}

inline void
NfsClient::TimeoutCallback()
{
    assert(context != nullptr);

    if (mount_finished) {
        assert(n_active_files == 0);

        DestroyContext();
        GError *error = g_error_new_literal(nfs_client_quark(), 0,
                                            "Idle timeout");
        handler.OnNfsClientClosed(error);
        pool_unref(&pool);
    } else {
        mount_finished = true;

        GError *error = g_error_new_literal(nfs_client_quark(), 0,
                                            "Mount timeout");
        MountError(error);
    }

    pool_commit();
}

/*
 * libnfs callbacks
 *
 */

inline void
NfsClient::MountCallback(int status, struct nfs_context *nfs, void *data)
{
    mount_finished = true;

    if (status < 0) {
        postponed_mount_error =
            nfs_client_new_error(status, nfs, data,
                                 "nfs_mount_async() failed");
        return;
    }

    postponed_mount_error = nullptr;
    n_active_files = 0;
}

static void
nfs_mount_cb(int status, struct nfs_context *nfs, void *data,
             void *private_data)
{
    auto &client = *(NfsClient *)private_data;
    client.MountCallback(status, nfs, data);
}

bool
NfsClient::Mount(const char *server, const char *exportname,
                 CancellablePointer &cancel_ptr)
{
    assert(context != nullptr);
    assert(!in_service);

    GError *error = nullptr;
    if (!MountAsync(server, exportname, nfs_mount_cb, this, &error)) {
        MountError(error);
        return false;
    }

    fd_set_cloexec(nfs_get_fd(context), true);

    AddEvent();

    timeout_event.Add(nfs_client_mount_timeout);

    cancel_ptr = *this;

    return true;
}

inline void
NfsFile::FstatCallback(int status, struct nfs_context *nfs, void *data)
{
    assert(state == NfsFile::PENDING_FSTAT);

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_fstat_async() failed");
        Abort(error);
        g_error_free(error);
        return;
    }

    const struct stat *st = (const struct stat *)data;

    if (!S_ISREG(st->st_mode)) {
        GError *error = g_error_new_literal(errno_quark(), ENOENT,
                                            "Not a regular file");
        Abort(error);
        g_error_free(error);
        return;
    }

    stat = *st;
    state = NfsFile::IDLE;
    expire_event.Add(nfs_file_expiry);

    Continue();
}

static void
nfs_fstat_cb(int status, struct nfs_context *nfs,
             void *data, void *private_data)
{
    NfsFile *const file = (NfsFile *)private_data;
    file->FstatCallback(status, nfs, data);
}

inline void
NfsFile::OpenCallback(int status, struct nfs_context *nfs, void *data)
{
    assert(state == NfsFile::PENDING_OPEN);

    if (status < 0) {
        GError *error = nfs_client_new_error(status, nfs, data,
                                             "nfs_open_async() failed");
        Abort(error);
        g_error_free(error);
        return;
    }

    nfsfh = (struct nfsfh *)data;
    state = NfsFile::PENDING_FSTAT;

    GError *error = nullptr;
    if (!client.FstatAsync(nfsfh, nfs_fstat_cb, this, &error)) {
        Abort(error);
        g_error_free(error);
        return;
    }
}

static void
nfs_open_cb(int status, gcc_unused struct nfs_context *nfs,
            void *data, void *private_data)
{
    NfsFile *const file = (NfsFile *)private_data;
    file->OpenCallback(status, nfs, data);
}

inline bool
NfsFile::Open(struct nfs_context *context, GError **error_r)
{
    if (nfs_open_async(context, path, O_RDONLY,
                       nfs_open_cb, this) != 0) {
        g_set_error(error_r, nfs_client_quark(), 0,
                    "nfs_open_async() failed: %s",
                    nfs_get_error(context));
        return false;
    }

    return true;
}

bool
NfsClient::MountAsync(const char *server, const char *exportname,
                      nfs_cb cb, void *private_data,
                      GError **error_r)
{
    if (nfs_mount_async(context, server, exportname,
                        cb, private_data) != 0) {
        g_set_error(error_r, nfs_client_quark(), 0,
                    "nfs_mount_async() failed: %s",
                    nfs_get_error(context));
        return false;
    }

    return true;
}

bool
NfsClient::ReadAsync(struct nfsfh *nfsfh, uint64_t offset, uint64_t count,
                     nfs_cb cb, void *private_data, GError **error_r)
{
    if (nfs_pread_async(context, nfsfh,
                        offset, count,
                        cb, private_data) != 0) {
        g_set_error(error_r, nfs_client_quark(), 0,
                    "nfs_pread_async() failed: %s",
                    nfs_get_error(context));
        return false;
    }

    UpdateEvent();
    return true;
}

bool
NfsClient::FstatAsync(struct nfsfh *nfsfh, nfs_cb cb, void *private_data,
                      GError **error_r)
{
    if (nfs_fstat_async(context, nfsfh, cb, private_data) != 0) {
        g_set_error(error_r, nfs_client_quark(), 0,
                    "nfs_fstat_async() failed: %s",
                    nfs_get_error(context));
        return false;
    }

    return true;
}

/*
 * constructor
 *
 */

void
nfs_client_new(EventLoop &event_loop, struct pool &pool,
               const char *server, const char *root,
               NfsClientHandler &handler,
               CancellablePointer &cancel_ptr)
{
    assert(server != nullptr);
    assert(root != nullptr);

    auto *context = nfs_init_context();
    if (context == nullptr) {
        handler.OnNfsMountError(g_error_new(nfs_client_quark(), 0,
                                            "nfs_init_context() failed"));
        return;
    }

    auto client = NewFromPool<NfsClient>(pool, event_loop, pool,
                                         handler, *context);
    client->Mount(server, root, cancel_ptr);
}

inline void
NfsClient::Destroy()
{
    assert(n_active_files == 0);

    timeout_event.Cancel();

    if (in_service) {
        postponed_destroy = true;
    } else {
        DestroyContext();
        CleanupFiles();
    }

    pool_unref(&pool);
}

void
nfs_client_free(NfsClient *client)
{
    assert(client != nullptr);

    client->Destroy();
}

inline void
NfsClient::OpenFile(struct pool &caller_pool,
                    const char *path,
                    NfsClientOpenFileHandler &_handler,
                    CancellablePointer &cancel_ptr)
{
    assert(context != nullptr);

    FileMap::insert_commit_data hint;
    auto result = file_map.insert_check(path, NfsFile::Compare(), hint);
    NfsFile *file;
    if (result.second) {
        struct pool *f_pool = pool_new_libc(&pool, "nfs_file");
        file = NewFromPool<NfsFile>(*f_pool, GetEventLoop(), *f_pool,
                                    *this, path);

        auto i = file_map.insert_commit(*file, hint);
        file_list.push_front(*file);

        GError *error = nullptr;
        if (!file->Open(context, &error)) {
            file_list.erase(file_list.iterator_to(*file));
            file_map.erase(i);
            file->Destroy();

            _handler.OnNfsOpenError(error);
            return;
        }
    } else
        file = &*result.first;

    const bool was_active = file->HasActiveHandles();

    auto handle = file->NewHandle(caller_pool);

    if (!was_active) {
        /* file has just got the first active handle */

        if (n_active_files == 0)
            /* cancel the idle timeout */
            timeout_event.Cancel();

        ++n_active_files;
    }

    UpdateEvent();

    if (file->IsReady()) {
        handle->Continue(_handler, file->GetStat());
    } else {
        handle->Wait(_handler, cancel_ptr);
    }
}

void
nfs_client_open_file(NfsClient *client, struct pool *caller_pool,
                     const char *path,
                     NfsClientOpenFileHandler &handler,
                     CancellablePointer &cancel_ptr)
{
    client->OpenFile(*caller_pool, path, handler, cancel_ptr);
}

inline void
NfsFileHandle::Close()
{
    assert(file.IsReady());

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
                    NfsClientReadFileHandler &handler)
{
    assert(state == IDLE);

    GError *error = nullptr;
    if (!file.ReadAsync(offset, length, nfs_read_cb, this, &error)) {
        handler.OnNfsReadError(error);
        return;
    }

    read_handler = &handler;
    state = PENDING;
}

void
nfs_client_read_file(NfsFileHandle *handle,
                     uint64_t offset, size_t length,
                     NfsClientReadFileHandler &handler)
{
    return handle->Read(offset, length, handler);
}
