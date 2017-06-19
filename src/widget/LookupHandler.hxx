/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_LOOKUP_HANDLER_HXX
#define BENG_PROXY_WIDGET_LOOKUP_HANDLER_HXX

#include "glibfwd.hxx"

struct Widget;

/**
 * A handler class for looking up a widget in a container.
 */
class WidgetLookupHandler {
public:
    virtual void WidgetFound(Widget &widget) = 0;
    virtual void WidgetNotFound() = 0;
    virtual void WidgetLookupError(GError *error) = 0;
};

#endif
