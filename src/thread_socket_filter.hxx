/*
 * A filtered_socket class that offloads the actual filtering to a
 * worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_SOCKET_FILTER_HXX
#define BENG_PROXY_THREAD_SOCKET_FILTER_HXX

#include "thread_job.h"
#include "defer_event.h"

#include <pthread.h>

typedef struct _GError GError;
struct ThreadSocketFilter;

struct ThreadSocketFilterHandler {
    bool (*run)(ThreadSocketFilter &f, GError **error_r, void *ctx);
    void (*destroy)(ThreadSocketFilter &f, void *ctx);
};

/**
 * A module for #filtered_socket that moves the filter to a thread
 * pool (see #thread_job).
 */
struct ThreadSocketFilter {
    struct thread_job job;

    struct pool *pool;

    struct thread_queue *queue;

    struct filtered_socket *socket;

    /**
     * The actual filter.  If this is NULL, then this object behaves
     * just like #buffered_socket.
     */
    const ThreadSocketFilterHandler *handler;
    void *handler_ctx;

    /**
     * This event moves a call out of the current stack frame.  It is
     * used by _schedule_write() to avoid calling
     * filtered_socket_invoke_write() directly.
     */
    struct defer_event defer_event;

    bool busy, done_pending;

    bool connected;

    /**
     * Does the handler expect more data?  It announced this by
     * returning BUFFERED_MORE.
     */
    bool expect_more;

    bool postponed_remaining;

    bool postponed_end;

    /**
     * Set to true when the thread queue hasn't yet released the
     * #thread_job.  The object will be destroyed in the "done"
     * callback.
     */
    bool postponed_destroy;

    /**
     * True when the client has called
     * filtered_socket_schedule_read().
     */
    bool want_read;

    /**
     * Was _schedule_read() forwarded?
     */
    bool read_scheduled;

    /**
     * True when the client has called
     * filtered_socket_schedule_write().
     */
    bool want_write;

    struct timeval read_timeout_buffer;
    const struct timeval *read_timeout;

    pthread_mutex_t mutex;

    /**
     * If this is set, an error was caught inside the thread, and
     * shall be forwarded to the main thread.
     */
    GError *error;

    /**
     * A buffer of input data that was not yet handled by the filter.
     * It will be passed to the filter, and after that, it will go to
     * #decrypted_input.
     *
     * This gets fed from buffered_socket::input.  We need another
     * buffer because buffered_socket is not thread-safe, while this
     * buffer is protected by the #mutex.
     */
    struct fifo_buffer *encrypted_input;

    /**
     * A buffer of input data that was handled by the filter.  It will
     * be passed to the handler.
     */
    struct fifo_buffer *decrypted_input;

    /**
     * A buffer of output data that was not yet handled by the filter.
     * Once it was filtered, it will be written to #encrypted_output.
     */
    struct fifo_buffer *plain_output;

    /**
     * A buffer of output data that has been filtered already, and
     * will be written to the socket.
     */
    struct fifo_buffer *encrypted_output;
};

ThreadSocketFilter *
thread_socket_filter_new(struct pool *pool,
                         struct thread_queue *queue,
                         const ThreadSocketFilterHandler *handler,
                         void *ctx);

extern const struct socket_filter thread_socket_filter;

#endif
