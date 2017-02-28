/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef GEXCEPTION_HXX
#define GEXCEPTION_HXX

#include "util/Exception.hxx"

#include <glib.h>

#include <exception>

/**
 * A GQuark for std::exception.
 */
G_GNUC_CONST
static inline GQuark
exception_quark()
{
    return g_quark_from_static_string("std::exception");
}

static inline void
SetGError(GError **error_r, const std::exception &e)
{
    g_set_error_literal(error_r, exception_quark(), 0,
                        GetFullMessage(e).c_str());
}

static inline GError *
ToGError(const std::exception &e)
{
    return g_error_new_literal(exception_quark(), 0, e.what());
}

GError *
ToGError(std::exception_ptr ep);

#endif
