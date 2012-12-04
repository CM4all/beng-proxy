/*
 * A library of GQuark constants.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_QUARK_H
#define BENG_PROXY_QUARK_H

#include <glib.h>

#include <errno.h>

G_GNUC_CONST
static inline GQuark
errno_quark(void)
{
    return g_quark_from_static_string("errno");
}

static inline void
set_error_errno2(GError **error_r, int code)
{
    g_set_error_literal(error_r, errno_quark(), code, g_strerror(code));
}

static inline void
set_error_errno(GError **error_r)
{
    set_error_errno2(error_r, errno);
}

static inline void
set_error_errno_msg2(GError **error_r, int code, const char *msg)
{
    g_set_error(error_r, errno_quark(), code, "%s: %s", msg, g_strerror(code));
}

static inline void
set_error_errno_msg(GError **error_r, const char *msg)
{
    set_error_errno_msg2(error_r, errno, msg);
}

static inline GError *
new_error_errno_msg2(int code, const char *msg)
{
    return g_error_new(errno_quark(), code, "%s: %s", msg, g_strerror(code));
}

static inline GError *
new_error_errno_msg(const char *msg)
{
    return new_error_errno_msg2(errno, msg);
}

static inline GError *
new_error_errno2(int code)
{
    return g_error_new_literal(errno_quark(), code, g_strerror(code));
}

static inline GError *
new_error_errno(void)
{
    return new_error_errno2(errno);
}

#endif
