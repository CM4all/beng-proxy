/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef GEXCEPTION_HXX
#define GEXCEPTION_HXX

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

void
SetGError(GError **error_r, const std::exception &e);

GError *
ToGError(const std::exception &e);

GError *
ToGError(std::exception_ptr ep);

#endif
