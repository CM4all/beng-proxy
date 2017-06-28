/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COMPLETION_H
#define BENG_PROXY_COMPLETION_H

/**
 * A return value type which indicates the state of the desired
 * operation.
 */
enum class Completion {
    /**
     * The operation completed.
     */
    DONE,

    /**
     * Partial completion.  More data is needed to complete the
     * operation.  If no more data can be obtained (e.g. because the
     * socket may be closed already), the recipient of this value
     * shall abort the operation.
     */
    MORE,

    /**
     * The object has been closed/destroyed.  Details have been
     * supplied to the handler.
     */
    CLOSED,
};

#endif
