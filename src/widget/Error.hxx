/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_ERROR_HXX
#define BENG_PROXY_WIDGET_ERROR_HXX

#include <glib.h>

enum class WidgetErrorCode {
    UNSPECIFIED,

    /**
     * The content-type of the server's response does not meet our
     * expectations.
     */
    WRONG_TYPE,

    /**
     * The response body is encoded in an unsupported way.
     */
    UNSUPPORTED_ENCODING,

    /**
     * The requested view does not exist.
     */
    NO_SUCH_VIEW,

    /**
     * Looking for a child widget inside a widget that is not a
     * container.
     */
    NOT_A_CONTAINER,

    /**
     * The client request is forbidden due to formal reasons.
     */
    FORBIDDEN,
};

static inline GQuark
widget_quark()
{
    return g_quark_from_static_string("widget");
}

#endif
