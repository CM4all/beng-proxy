/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_CHECK_ERROR_HXX
#define PG_CHECK_ERROR_HXX

#include "Result.hxx"
#include "Error.hxx"

/**
 * Check if the #PgResult contains an error state, and throw a
 * #PgError based on this condition.  If the #PgResult did not contain
 * an error, it is returned as-is.
 */
static inline PgResult
CheckError(PgResult &&result)
{
    if (result.IsError())
        throw PgError(std::move(result));

    return std::move(result);
}

#endif
