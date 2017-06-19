/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Widget.hxx"
#include "Class.hxx"
#include "View.hxx"
#include "Ref.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "util/HexFormat.h"

#include <string.h>
#include <assert.h>

static bool
valid_prefix_start_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        ch == '_';
}

static bool
valid_prefix_char(char ch)
{
    return valid_prefix_start_char(ch) ||
        (ch >= '0' && ch <= '9');
}

static size_t
count_invalid_chars(const char *p)
{
    assert(*p != 0);

    size_t n = 0;
    if (!valid_prefix_start_char(*p))
        ++n;

    for (++p; *p != 0; ++p)
        if (!valid_prefix_char(*p))
            ++n;

    return n;
}

static char *
quote_byte(char *p, uint8_t ch)
{
    *p++ = '_';
    format_uint8_hex_fixed(p, ch);
    return p + 2;
}

static const char *
quote_prefix(struct pool *pool, const char *p)
{
    if (*p == 0)
        return p;

    size_t n_quotes = count_invalid_chars(p);
    if (n_quotes == 0)
        /* no escaping needed */
        return p;

    const size_t src_length = strlen(p);
    char *buffer = (char *) p_malloc(pool, src_length + n_quotes * 2 + 1);
    char *q = buffer;

    if (!valid_prefix_start_char(*p))
        q = quote_byte(q, *p++);

    while (*p != 0) {
        if (!valid_prefix_char(*p))
            q = quote_byte(q, *p++);
        else
            *q++ = *p++;
    }

    *q = 0;
    return buffer;
}

void
Widget::SetId(const StringView _id)
{
    assert(parent != nullptr);
    assert(!_id.IsEmpty());

    id = p_strdup(pool, _id);

    const char *p = parent->GetIdPath();
    if (p != nullptr)
        id_path = *p == 0
            ? id
            : p_strcat(&pool, p, WIDGET_REF_SEPARATOR_S, id, nullptr);

    p = parent->GetPrefix();
    if (p != nullptr)
        prefix = p_strcat(&pool, p, quote_prefix(&pool, id), "__", nullptr);
}

void
Widget::SetClassName(const StringView _class_name)
{
    assert(parent != nullptr);
    assert(class_name == nullptr);
    assert(cls == nullptr);

    class_name = p_strdup(pool, _class_name);
    quoted_class_name = quote_prefix(&pool, class_name);
}

const char *
Widget::GetLogName() const
{
    if (lazy.log_name != nullptr)
        return lazy.log_name;

    if (class_name == nullptr)
        return id;

    if (id_path == nullptr) {
        if (id != nullptr)
            return lazy.log_name = p_strcat(&pool, class_name,
                                            "#(null)" WIDGET_REF_SEPARATOR_S,
                                            id_path, nullptr);

        return class_name;
    }

    return lazy.log_name = p_strcat(&pool, class_name, "#", id_path, nullptr);
}

bool
Widget::IsContainerByDefault() const
{
    const WidgetView *v = GetDefaultView();
    return v != nullptr && v->IsContainer();
}

bool
Widget::HasProcessor() const
{
    const WidgetView *v = GetTransformationView();
    assert(v != nullptr);
    return v->HasProcessor();
}

bool
Widget::IsContainer() const
{
    const WidgetView *v = GetTransformationView();
    return v != nullptr && v->IsContainer();
}

Widget *
Widget::FindChild(const char *child_id)
{
    assert(child_id != nullptr);

    for (auto &child : children)
        if (child.id != nullptr && strcmp(child.id, child_id) == 0)
            return &child;

    return nullptr;
}

void
Widget::CheckHost(const char *host, const char *site_name) const
{
    assert(cls != nullptr);

    cls->CheckHost(host, site_name);
}

bool
widget_check_recursion(const Widget *widget)
{
    unsigned depth = 0;

    assert(widget != nullptr);

    do {
        if (++depth >= 8)
            return true;

        widget = widget->parent;
    } while (widget != nullptr);

    return false;
}

void
Widget::Cancel()
{
    if (from_request.body != nullptr)
        /* we are not going to consume the request body, so abort
           it */
        istream_free_unused(&from_request.body);

    if (for_focused.body != nullptr)
        /* the request body was not forwarded to the focused widget,
           so discard it */
        istream_free_unused(&for_focused.body);
}
