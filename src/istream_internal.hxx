/*
 * Functions for implementing an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_INTERNAL_H
#define __BENG_ISTREAM_INTERNAL_H

#include "istream.hxx"
#include "istream_invoke.hxx"
#include "istream_new.hxx"

/**
 * Checks if the istream handler supporst the specified file
 * descriptor type.
 */
static inline bool
istream_check_direct(const struct istream *istream, enum istream_direct type)
{
    return (istream->handler_direct & type) != 0;
}

#endif
