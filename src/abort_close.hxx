/*
 * A wrapper for an Cancellable which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ABORT_CLOSE_HXX
#define BENG_PROXY_ABORT_CLOSE_HXX

struct pool;
class Istream;
class CancellablePointer;

/**
 * @param istream the istream to be closed on abort; it should be
 * allocated from the specified pool, and must not have a handler
 */
CancellablePointer &
async_close_on_abort(struct pool &pool, Istream &istream,
                     CancellablePointer &cancel_ptr);

/**
 * Same as async_close_on_abort(), but allows #istream to be NULL.
 */
CancellablePointer &
async_optional_close_on_abort(struct pool &pool, Istream *istream,
                              CancellablePointer &cancel_ptr);

#endif
