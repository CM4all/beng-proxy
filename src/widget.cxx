/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_view.hxx"
#include "pool.hxx"
#include "format.h"
#include "istream/istream.hxx"

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

    id = p_strdup(*pool, _id);

    const char *p = parent->GetIdPath();
    if (p != nullptr)
        lazy.path = *p == 0
            ? id
            : p_strcat(pool, p, WIDGET_REF_SEPARATOR_S, id, nullptr);

    p = parent->GetPrefix();
    if (p != nullptr)
        lazy.prefix = p_strcat(pool, p, quote_prefix(pool, id), "__", nullptr);
}

void
Widget::SetClassName(const StringView _class_name)
{
    assert(parent != nullptr);
    assert(class_name == nullptr);
    assert(cls == nullptr);

    class_name = p_strdup(*pool, _class_name);
    lazy.quoted_class_name = quote_prefix(pool, class_name);
}

const char *
Widget::GetLogName() const
{
    if (lazy.log_name != nullptr)
        return lazy.log_name;

    if (class_name == nullptr)
        return id;

    const char *id_path = GetIdPath();
    if (id_path == nullptr) {
        if (id != nullptr)
            return lazy.log_name = p_strcat(pool, class_name,
                                            "#(null)" WIDGET_REF_SEPARATOR_S,
                                            id_path, nullptr);

        return class_name;
    }

    return lazy.log_name = p_strcat(pool, class_name, "#", id_path, nullptr);
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

static bool
widget_check_untrusted_host(const Widget *widget, const char *host)
{
    assert(widget->cls != nullptr);

    if (widget->cls->untrusted_host == nullptr)
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;

    if (host == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    /* untrusted widget only allowed on matching untrusted host
       name */
    return strcmp(host, widget->cls->untrusted_host) == 0;
}

static bool
widget_check_untrusted_prefix(const Widget *widget, const char *host)
{
    assert(widget->cls != nullptr);

    if (widget->cls->untrusted_prefix == nullptr)
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;

    if (host == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    /* untrusted widget only allowed on matching untrusted host
       name */
    size_t length = strlen(widget->cls->untrusted_prefix);
    return memcmp(host, widget->cls->untrusted_prefix, length) == 0 &&
        host[length] == '.';
}

static bool
widget_check_untrusted_site_suffix(const Widget *widget,
                                   const char *host, const char *site_name)
{
    assert(widget->cls != nullptr);

    if (widget->cls->untrusted_site_suffix == nullptr)
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;

    if (host == nullptr || site_name == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    size_t site_name_length = strlen(site_name);
    return memcmp(host, site_name, site_name_length) == 0 &&
        host[site_name_length] == '.' &&
        strcmp(host + site_name_length + 1,
               widget->cls->untrusted_site_suffix) == 0;
}

static bool
widget_check_untrusted_raw_site_suffix(const Widget *widget,
                                       const char *host, const char *site_name)
{
    assert(widget->cls != nullptr);

    if (widget->cls->untrusted_raw_site_suffix == nullptr)
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;

    if (host == nullptr || site_name == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    size_t site_name_length = strlen(site_name);
    return memcmp(host, site_name, site_name_length) == 0 &&
        strcmp(host + site_name_length,
               widget->cls->untrusted_raw_site_suffix) == 0;
}

bool
Widget::CheckHost(const char *host, const char *site_name) const
{
    assert(cls != nullptr);

    if (cls->untrusted_host != nullptr)
        return widget_check_untrusted_host(this, host);
    else if (cls->untrusted_prefix != nullptr)
        return widget_check_untrusted_prefix(this, host);
    else if (cls->untrusted_site_suffix != nullptr)
        return widget_check_untrusted_site_suffix(this, host, site_name);
    else if (cls->untrusted_raw_site_suffix != nullptr)
        return widget_check_untrusted_raw_site_suffix(this, host, site_name);
    else
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;
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
widget_cancel(Widget *widget)
{
    if (widget->from_request.body != nullptr)
        /* we are not going to consume the request body, so abort
           it */
        istream_free_unused(&widget->from_request.body);

    if (widget->for_focused.body != nullptr)
        /* the request body was not forwarded to the focused widget,
           so discard it */
        istream_free_unused(&widget->for_focused.body);
}
