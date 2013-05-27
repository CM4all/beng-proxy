/*
 * Glue for libnfs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_client.h"
#include "pool.h"
#include "async.h"
#include "hashmap.h"
#include "gerrno.h"

#include <inline/list.h>

#include <nfsc/libnfs.h>
#include <event.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/stat.h>

struct nfs_file_handle {
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

    const struct nfs_client_open_file_handler *open_handler;
    const struct nfs_client_read_file_handler *read_handler;
    void *handler_ctx;

    struct async_operation operation;

    /**
     * Is a request pending inside libnfs?  This object can only be
     * freed when all libnfs operations referencing this object are
     * finished.
     */
    bool pending;

    /**
     * This is true if istream_close() has been called by the istream
     * handler.  This object cannot be destroyed until libnfs has
     * released the reference to this object (queued async call with
     * private_data pointing to this object).  As soon as libnfs calls
     * back, the object will finally be destroyed.
     */
    bool closed;
};

struct nfs_file {
    /**
     * @see nfs_client::file_list
     */
    struct list_head siblings;

    struct pool *pool;
    struct nfs_client *client;
    const char *path;

    struct list_head handles;
    unsigned n_active_handles;

    /**
     * If this is NULL, then nfs_open_async() has not finished yet.
     */
    struct nfsfh *nfsfh;

    /**
     * If st_mode is 0, then nfs_fstat_async() has not finished yet.
     */
    struct stat stat;

    bool locked;
};

struct nfs_client {
    struct pool *pool;

    const struct nfs_client_handler *handler;
    void *handler_ctx;

    struct nfs_context *context;

    struct event event;

    struct list_head file_list;

    struct hashmap *file_map;

    unsigned n_active_files;

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

static GError *
nfs_client_new_error(const char *msg, int status, void *data)
{
    assert(msg != NULL);
    assert(status < 0);

    const char *msg2 = data != NULL && *(const char *)data != 0
        ? (const char *)data
        : g_strerror(-status);

    return g_error_new(errno_quark(), -status, "%s: %s", msg, msg2);
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

static bool
nfs_file_is_ready(const struct nfs_file *file)
{
    return file->nfsfh != NULL && file->stat.st_mode != 0;
}

static void
nfs_file_deactivate(struct nfs_file *file)
{
    struct nfs_client *const client = file->client;

    assert(client->n_active_files > 0);
    --client->n_active_files;

    // TODO: abort connection after the last file was aborted?
}

/**
 * Mark this object "inactive".  Call nfs_file_request_release() after
 * all references by libnfs have been cleared.
 */
static void
nfs_file_handle_deactivate(struct nfs_file_handle *handle)
{
    struct nfs_file *const file = handle->file;

    assert(file->n_active_handles > 0);
    --file->n_active_handles;

    if (file->n_active_handles == 0)
        nfs_file_deactivate(file);
}

static bool
nfs_file_handle_can_release(const struct nfs_file_handle *handle)
{
    assert(handle != NULL);
    assert(handle->file != NULL);
    assert(nfs_file_is_ready(handle->file));

    return !handle->pending;
}

/**
 * Release an "inactive" handle.  Must have called
 * nfs_file_handle_deactivate() prior to this.
 */
static void
nfs_file_handle_release(struct nfs_file_handle *request)
{
    assert(!nfs_file_is_ready(request->file) ||
           nfs_file_handle_can_release(request));

    list_remove(&request->siblings);
    pool_unref(request->pool);
}

static void
nfs_file_handle_abort(struct nfs_file_handle *handle, GError *error)
{
    nfs_file_handle_deactivate(handle);

    handle->open_handler->error(error, handle->handler_ctx);

    pool_unref(handle->caller_pool);
    pool_unref(handle->pool);
}

static void
nfs_file_abort_requests(struct nfs_file *file, GError *error)
{
    struct list_head *const head = &file->handles;
    while (!list_empty(head)) {
        struct nfs_file_handle *handle =
            (struct nfs_file_handle *)head->next;
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
nfs_client_abort_file(struct nfs_client *client, struct nfs_file *file,
                      GError *error)
{
    nfs_file_abort_requests(file, error);

    list_remove(&file->siblings);
    hashmap_remove(client->file_map, file->path);
    pool_unref(file->pool);
}

static void
nfs_client_destroy_context(struct nfs_client *client)
{
    assert(client != NULL);
    assert(client->context != NULL);
    assert(!client->in_service);

    event_del(&client->event);
    nfs_destroy_context(client->context);
    client->context = NULL;
}

/**
 * Mounting has failed.  Destroy the #nfs_client object and report the
 * error to the handler.
 */
static void
nfs_client_mount_error(struct nfs_client *client, GError *error)
{
    assert(client != NULL);
    assert(client->context != NULL);
    assert(!client->in_service);

    nfs_client_destroy_context(client);

    client->handler->mount_error(error, client->handler_ctx);
    pool_unref(client->pool);
}

static void
nfs_client_cleanup_files(struct nfs_client *client)
{
    for (struct nfs_file *file = (struct nfs_file *)client->file_list.next;
         &file->siblings != &client->file_list;) {
        struct nfs_file *next = (struct nfs_file *)file->siblings.next;

        if (list_empty(&file->handles)) {
            assert(file->n_active_handles == 0);

            list_remove(&file->siblings);
            hashmap_remove(client->file_map, file->path);
            pool_unref(file->pool);
        }

        file = next;
    }
}

static void
nfs_client_abort_all_files(struct nfs_client *client, GError *error)
{
    struct list_head *const head = &client->file_list;
    while (!list_empty(head)) {
        struct nfs_file *file = (struct nfs_file *)head->next;

        nfs_client_abort_file(client, file, error);
    }
}

static void
nfs_client_error(struct nfs_client *client, GError *error)
{
    if (client->mount_finished) {
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
nfs_client_add_event(struct nfs_client *client)
{
    event_set(&client->event, nfs_get_fd(client->context),
              libnfs_to_libevent(nfs_which_events(client->context)),
              nfs_client_event_callback, client);
    event_add(&client->event, NULL);
}

static void
nfs_client_update_event(struct nfs_client *client)
{
    if (client->in_event)
        return;

    event_del(&client->event);
    nfs_client_add_event(client);
}

static void
nfs_read_cb(int status, gcc_unused struct nfs_context *nfs,
            void *data, void *private_data)
{
    struct nfs_file_handle *const handle = private_data;

    assert(handle->pending);
    handle->pending = false;

    if (handle->closed) {
        nfs_file_handle_release(handle);
        return;
    }

    if (status == 0) {
        GError *error = nfs_client_new_error("nfs_pread_async() failed",
                                             status, data);
        handle->read_handler->error(error, handle->handler_ctx);
        return;
    }

    handle->read_handler->data(data, handle->handler_ctx);
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

    assert(!file->locked);
    file->locked = true;

    while (!list_empty(&tmp_head)) {
        struct nfs_file_handle *handle =
            (struct nfs_file_handle *)tmp_head.next;
        assert(handle->file == file);

        list_remove(&handle->siblings);
        list_add(&handle->siblings, &file->handles);

        handle->pending = false;
        handle->closed = false;

        struct pool *const caller_pool = handle->caller_pool;

        handle->open_handler->ready(handle, &file->stat,
                                    handle->handler_ctx);
        pool_unref(caller_pool);
    }

    assert(file->locked);
    file->locked = false;
}

/*
 * async operation
 *
 */

static struct nfs_file_handle *
operation_to_nfs_file_handle(struct async_operation *ao)
{
    return (struct nfs_file_handle *)(((char *)ao) - offsetof(struct nfs_file_handle,
                                                              operation));
}

static void
nfs_file_open_operation_abort(struct async_operation *ao)
{
    struct nfs_file_handle *const handle = operation_to_nfs_file_handle(ao);

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

static struct nfs_client *
operation_to_nfs_client(struct async_operation *ao)
{
    return (struct nfs_client *)(((char *)ao) - offsetof(struct nfs_client,
                                                         mount_operation));
}

static void
nfs_client_mount_abort(struct async_operation *ao)
{
    struct nfs_client *const client = operation_to_nfs_client(ao);
    assert(client->context != NULL);
    assert(!client->mount_finished);
    assert(!client->in_service);

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
nfs_client_event_callback(gcc_unused int fd, short event, void *ctx)
{
    struct nfs_client *client = ctx;
    assert(client->context != NULL);

    struct pool *const pool = client->pool;
    pool_ref(pool);

    const bool was_mounted = client->mount_finished;

    assert(!client->in_event);
    client->in_event = true;

    assert(!client->in_service);
    client->in_service = true;
    client->postponed_destroy = false;

    int result = nfs_service(client->context, libevent_to_libnfs(event));

    assert(client->context != NULL);
    assert(client->in_service);
    client->in_service = false;

    if (client->postponed_destroy) {
        /* somebody has called nfs_client_free() while we were inside
           nfs_service() */
        nfs_client_destroy_context(client);
        nfs_client_cleanup_files(client);
    } else if (!was_mounted && client->mount_finished) {
        if (client->postponed_mount_error != NULL)
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

    if (client->context != NULL)
        nfs_client_add_event(client);

    pool_unref(pool);
    pool_commit();
}

/*
 * libnfs callbacks
 *
 */

static void
nfs_mount_cb(int status, gcc_unused struct nfs_context *nfs, void *data,
             void *private_data)
{
    struct nfs_client *client = private_data;

    client->mount_finished = true;

    if (status < 0) {
        client->postponed_mount_error =
            nfs_client_new_error("nfs_mount_async() failed", status, data);
        return;
    }

    client->postponed_mount_error = NULL;

    client->file_map = hashmap_new(client->pool, 59);
    list_init(&client->file_list);
    client->n_active_files = 0;
}

static void
nfs_fstat_cb(int status, gcc_unused struct nfs_context *nfs,
             void *data, void *private_data)
{
    struct nfs_file *const file = private_data;
    assert(file->nfsfh != NULL);

    struct nfs_client *const client = file->client;

    if (status < 0) {
        GError *error = nfs_client_new_error("nfs_fstat_async() failed",
                                             status, data);
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

    nfs_file_continue(file);
}

static void
nfs_open_cb(int status, gcc_unused struct nfs_context *nfs,
            void *data, void *private_data)
{
    struct nfs_file *const file = private_data;
    assert(file->nfsfh == NULL);

    struct nfs_client *const client = file->client;

    if (status < 0) {
        GError *error = nfs_client_new_error("nfs_open_async() failed",
                                             status, data);
        nfs_client_abort_file(client, file, error);
        g_error_free(error);
        return;
    }

    file->nfsfh = data;
    file->stat.st_mode = 0;

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
               const struct nfs_client_handler *handler, void *ctx,
               struct async_operation_ref *async_ref)
{
    assert(pool != NULL);
    assert(server != NULL);
    assert(root != NULL);
    assert(handler != NULL);
    assert(handler->ready != NULL);
    assert(handler->mount_error != NULL);
    assert(handler->closed != NULL);

    struct nfs_client *client = p_malloc(pool, sizeof(*client));
    client->pool = pool;

    client->context = nfs_init_context();
    if (client->context == NULL) {
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

    nfs_client_add_event(client);

    async_init(&client->mount_operation, &nfs_client_mount_operation);
    async_ref_set(async_ref, &client->mount_operation);
}

void
nfs_client_free(struct nfs_client *client)
{
    assert(client != NULL);
    assert(client->n_active_files == 0);

    if (client->in_service) {
        client->postponed_destroy = true;
    } else {
        nfs_client_destroy_context(client);
        nfs_client_cleanup_files(client);
    }

    pool_unref(client->pool);
}

void
nfs_client_open_file(struct nfs_client *client, struct pool *caller_pool,
                     const char *path,
                     const struct nfs_client_open_file_handler *handler,
                     void *ctx,
                     struct async_operation_ref *async_ref)
{
    assert(client->context != NULL);

    struct nfs_file *file = hashmap_get(client->file_map, path);
    if (file == NULL) {
        struct pool *f_pool = pool_new_libc(client->pool, "nfs_file");
        file = p_malloc(f_pool, sizeof(*file));
        file->pool = f_pool;
        file->client = client;
        file->path = p_strdup(f_pool, path);
        list_init(&file->handles);
        file->n_active_handles = 0;
        file->nfsfh = NULL;
        file->locked = false;

        hashmap_add(client->file_map, file->path, file);
        list_add(&file->siblings, &client->file_list);

        if (nfs_open_async(client->context, file->path, O_RDONLY,
                           nfs_open_cb, file) != 0) {
            pool_unref(file->pool);

            GError *error = g_error_new(nfs_client_quark(), 0,
                                        "nfs_open_async() failed: %s",
                                        nfs_get_error(client->context));
            handler->error(error, ctx);
            return;
        }
    }

    struct pool *r_pool = pool_new_libc(file->pool, "nfs_file_request");

    struct nfs_file_handle *handle = p_malloc(r_pool, sizeof(*handle));
    handle->file = file;
    handle->pool = r_pool;
    handle->caller_pool = caller_pool;

    list_add(&handle->siblings, &file->handles);
    ++file->n_active_handles;
    if (file->n_active_handles == 1)
        ++client->n_active_files;

    nfs_client_update_event(client);

    if (nfs_file_is_ready(file))
        handler->ready(handle, &file->stat, ctx);
    else {
        handle->open_handler = handler;
        handle->handler_ctx = ctx;

        async_init(&handle->operation, &nfs_file_open_operation);
        async_ref_set(async_ref, &handle->operation);

        pool_ref(caller_pool);
    }
}

void
nfs_client_close_file(struct nfs_file_handle *handle)
{
    assert(!handle->closed);
    assert(nfs_file_is_ready(handle->file));

    handle->closed = true;

    nfs_file_handle_deactivate(handle);
    if (nfs_file_handle_can_release(handle))
        nfs_file_handle_release(handle);
}

void
nfs_client_read_file(struct nfs_file_handle *handle,
                     uint64_t offset, size_t length,
                     const struct nfs_client_read_file_handler *handler,
                     void *ctx)
{
    struct nfs_file *const file = handle->file;
    struct nfs_client *const client = file->client;

    assert(!handle->pending);
    assert(!handle->closed);

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
    handle->pending = true;

    nfs_client_update_event(client);
}
