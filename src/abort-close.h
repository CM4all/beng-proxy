/*
 * A wrapper for an async_operation which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ABORT_CLOSE_H
#define BENG_PROXY_ABORT_CLOSE_H

#include "istream.h"

struct async_operation_ref;

/**
 * @param istream the istream to be closed on abort; it should be
 * allocated from the specified pool, and must not have a handler
 */
struct async_operation_ref *
async_close_on_abort(pool_t pool, istream_t istream,
                     struct async_operation_ref *async_ref);

/**
 * Same as async_close_on_abort(), but allows #istream to be NULL.
 */
struct async_operation_ref *
async_optional_close_on_abort(pool_t pool, istream_t istream,
                              struct async_operation_ref *async_ref);

#endif
