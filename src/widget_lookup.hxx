/*
 * A handler class for looking up a widget in a container.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_LOOKUP_HXX
#define BENG_PROXY_WIDGET_LOOKUP_HXX

#include "glibfwd.hxx"

struct Widget;

class WidgetLookupHandler {
public:
    virtual void WidgetFound(Widget &widget) = 0;
    virtual void WidgetNotFound() = 0;
    virtual void WidgetLookupError(GError *error) = 0;
};

#endif
