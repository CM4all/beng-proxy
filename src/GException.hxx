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

GError *
ToGError(std::exception_ptr ep);

/**
 * Attempt to convert a #GError to a C++ exception (best-effort), and
 * throw it.
 */
void
ThrowGError(const GError &error);

/**
 * Like ThrowGError(), but also free the GError.
 */
void
ThrowFreeGError(GError *error);

/**
 * Attempt to convert a #GError to a C++ exception (best-effort), and
 * return it.
 */
std::exception_ptr
ToException(const GError &error);

#endif
