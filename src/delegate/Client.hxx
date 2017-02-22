/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_CLIENT_HXX
#define BENG_DELEGATE_CLIENT_HXX

struct pool;
class EventLoop;
class Lease;
class CancellablePointer;
class DelegateHandler;

/**
 * Opens a file with the delegate.
 *
 * @param fd the socket to the helper process
 */
void
delegate_open(EventLoop &event_loop, int fd, Lease &lease,
              struct pool *pool, const char *path,
              DelegateHandler &handler,
              CancellablePointer &cancel_ptr);

#endif
