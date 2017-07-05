/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Error.hxx"
#include "Widget.hxx"
#include "util/StringBuffer.hxx"
#include "util/StringFormat.hxx"

static StringBuffer<256>
FormatWidgetError(const Widget &widget, const char *msg)
{
    return StringFormat<256>("Error from widget '%s': %s",
                             widget.GetLogName(), msg);
}

WidgetError::WidgetError(const Widget &widget,
                         WidgetErrorCode _code, const char *_msg)
    :std::runtime_error(FormatWidgetError(widget, _msg).c_str()), code(_code)
{
}
