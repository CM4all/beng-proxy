/*
 * A filtered_socket class that offloads the actual filtering to a
 * worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_SOCKET_FILTER_HXX
#define BENG_PROXY_THREAD_SOCKET_FILTER_HXX

#include "thread_job.hxx"
#include "event/DeferEvent.hxx"
#include "SliceFifoBuffer.hxx"

#include <mutex>

typedef struct _GError GError;
struct SocketFilter;
struct FilteredSocket;
struct ThreadSocketFilter;
class ThreadQueue;

struct ThreadSocketFilterHandler {
    /**
     * Do the work.  This is run in an unspecified worker thread.  The
     * given #ThreadSocketFilter's mutex may be used for protection.
     */
    bool (*run)(ThreadSocketFilter &f, GError **error_r, void *ctx);

    /**
     * The #ThreadSocketFilter is about to be destroyed.
     */
    void (*destroy)(ThreadSocketFilter &f, void *ctx);
};

/**
 * A module for #filtered_socket that moves the filter to a thread
 * pool (see #thread_job).
 */
struct ThreadSocketFilter : ThreadJob {
    struct pool &pool;

    ThreadQueue &queue;

    FilteredSocket *socket;

    /**
     * The actual filter.  If this is NULL, then this object behaves
     * just like #buffered_socket.
     */
    const ThreadSocketFilterHandler &handler;
    void *handler_ctx;

    /**
     * This event moves a call out of the current stack frame.  It is
     * used by _schedule_write() to avoid calling
     * filtered_socket_invoke_write() directly.
     */
    DeferEvent defer_event;

    bool busy = false, done_pending = false;

    bool connected = true;

    /**
     * Does the handler expect more data?  It announced this by
     * returning BUFFERED_MORE.
     */
    bool expect_more = false;

    bool postponed_remaining = false;

    bool postponed_end = false;

    /**
     * Set to true when the thread queue hasn't yet released the
     * #thread_job.  The object will be destroyed in the "done"
     * callback.
     */
    bool postponed_destroy = false;

    /**
     * True when the client has called
     * filtered_socket_schedule_read().
     */
    bool want_read = false;

    /**
     * Was _schedule_read() forwarded?
     */
    bool read_scheduled = false;

    /**
     * True when the client has called
     * filtered_socket_schedule_write().
     */
    bool want_write = false;

    /**
     * True when #ThreadSocketFilterHandler's internal output buffers
     * are empty.  Set by #ThreadSocketFilterHandler::run() before
     * returning.
     *
     * Protected by #mutex.
     */
    bool drained = true;

    /**
     * True when no more input can be decrypted by
     * #ThreadSocketFilterHandler.  Will be set to true by
     * #ThreadSocketFilterHandler.
     *
     * Protected by #mutex.
     */
    bool input_eof = false;

    struct timeval read_timeout_buffer;
    const struct timeval *read_timeout = nullptr;

    std::mutex mutex;

    /**
     * If this is set, an error was caught inside the thread, and
     * shall be forwarded to the main thread.
     */
    GError *error = nullptr;

    /**
     * A buffer of input data that was not yet handled by the filter.
     * It will be passed to the filter, and after that, it will go to
     * #decrypted_input.
     *
     * This gets fed from buffered_socket::input.  We need another
     * buffer because buffered_socket is not thread-safe, while this
     * buffer is protected by the #mutex.
     */
    SliceFifoBuffer encrypted_input;

    /**
     * A buffer of input data that was handled by the filter.  It will
     * be passed to the handler.
     */
    SliceFifoBuffer decrypted_input;

    /**
     * A buffer of output data that was not yet handled by the filter.
     * Once it was filtered, it will be written to #encrypted_output.
     */
    SliceFifoBuffer plain_output;

    /**
     * A buffer of output data that has been filtered already, and
     * will be written to the socket.
     */
    SliceFifoBuffer encrypted_output;

    ThreadSocketFilter(struct pool &pool,
                       ThreadQueue &queue,
                       const ThreadSocketFilterHandler &handler,
                       void *ctx);

    ThreadSocketFilter(const ThreadSocketFilter &) = delete;

    ~ThreadSocketFilter();

    /* virtual methods from class ThreadJob */
    void Run() final;
    void Done() final;

private:
    void DeferCallback();
};

ThreadSocketFilter *
thread_socket_filter_new(struct pool &pool,
                         ThreadQueue &queue,
                         const ThreadSocketFilterHandler &handler,
                         void *ctx);

extern const SocketFilter thread_socket_filter;

#endif
