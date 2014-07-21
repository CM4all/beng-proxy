/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_view.hxx"
#include "strref-pool.h"
#include "format.h"
#include "istream.h"

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
widget::SetId(const struct strref &_id)
{
    assert(parent != nullptr);
    assert(!strref_is_empty(&_id));

    id = strref_dup(pool, &_id);

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
widget::SetClassName(const struct strref &_class_name)
{
    assert(parent != nullptr);
    assert(class_name == nullptr);
    assert(cls == nullptr);

    class_name = strref_dup(pool, &_class_name);
    lazy.quoted_class_name = quote_prefix(pool, class_name);
}

bool
widget_is_container_by_default(const struct widget *widget)
{
    const struct widget_view *view = widget_get_default_view(widget);
    return view != nullptr && view->IsContainer();
}

bool
widget_has_processor(const struct widget *widget)
{
    const struct widget_view *view = widget_get_view(widget);
    assert(view != nullptr);
    return view->HasProcessor();
}

bool
widget_is_container(const struct widget *widget)
{
    const struct widget_view *view = widget_get_transformation_view(widget);
    return view != nullptr && view->IsContainer();
}

struct widget *
widget::FindChild(const char *child_id)
{
    assert(child_id != nullptr);

    struct widget *child;
    for (child = (struct widget *)children.next;
         child != (struct widget *)&children;
         child = (struct widget *)child->siblings.next) {
        if (child->id != nullptr && strcmp(child->id, child_id) == 0)
            return child;
    }

    return nullptr;
}

static bool
widget_check_untrusted_host(const struct widget *widget, const char *host)
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
widget_check_untrusted_prefix(const struct widget *widget, const char *host)
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
widget_check_untrusted_site_suffix(const struct widget *widget,
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

bool
widget_check_host(const struct widget *widget, const char *host,
                  const char *site_name)
{
    assert(widget->cls != nullptr);

    if (widget->cls->untrusted_host != nullptr)
        return widget_check_untrusted_host(widget, host);
    else if (widget->cls->untrusted_prefix != nullptr)
        return widget_check_untrusted_prefix(widget, host);
    else if (widget->cls->untrusted_site_suffix != nullptr)
        return widget_check_untrusted_site_suffix(widget, host, site_name);
    else
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;
}

bool
widget_check_recursion(const struct widget *widget)
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
widget_cancel(struct widget *widget)
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
