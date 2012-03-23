/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_QUARK_H
#define BENG_PROXY_WIDGET_QUARK_H

#include <glib.h>

enum widget_error {
    WIDGET_ERROR_UNSPECIFIED,

    /**
     * The widget server did not send a response body, when one was
     * expected.
     */
    WIDGET_ERROR_EMPTY,

    /**
     * The content-type of the server's response does not meet our
     * expectations.
     */
    WIDGET_ERROR_WRONG_TYPE,

    /**
     * The response body is encoded in an unsupported way.
     */
    WIDGET_ERROR_UNSUPPORTED_ENCODING,

    /**
     * The requested view does not exist.
     */
    WIDGET_ERROR_NO_SUCH_VIEW,

    /**
     * Looking for a child widget inside a widget that is not a
     * container.
     */
    WIDGET_ERROR_NOT_A_CONTAINER,

    /**
     * The client request is forbidden due to formal reasons.
     */
    WIDGET_ERROR_FORBIDDEN,
};

static inline GQuark
widget_quark(void)
{
    return g_quark_from_static_string("widget");
}

#endif
