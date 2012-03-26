/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "widget-class.h"
#include "widget-view.h"
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
    char *buffer = p_malloc(pool, src_length + n_quotes * 2 + 1), *q = buffer;

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
widget_set_id(struct widget *widget, struct pool *pool,
              const struct strref *id)
{
    const char *p;

    assert(id != NULL);
    assert(widget->parent != NULL);
    assert(!strref_is_empty(id));

    widget->id = strref_dup(pool, id);

    p = widget_path(widget->parent);
    if (p != NULL)
        widget->lazy.path = *p == 0
            ? widget->id
            : p_strcat(pool, p, WIDGET_REF_SEPARATOR_S, widget->id, NULL);

    p = widget_prefix(widget->parent);
    if (p != NULL)
        widget->lazy.prefix = p_strcat(pool, p,
                                       quote_prefix(pool, widget->id),
                                       "__", NULL);
}

void
widget_set_class_name(struct widget *widget, struct pool *pool,
                      const struct strref *class_name)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);
    assert(widget->class_name == NULL);
    assert(widget->class == NULL);
    assert(pool != NULL);
    assert(class_name != NULL);

    widget->class_name = strref_dup(pool, class_name);
    widget->lazy.quoted_class_name = quote_prefix(pool, widget->class_name);
}

bool
widget_is_container_by_default(const struct widget *widget)
{
    const struct widget_view *view = widget_get_default_view(widget);
    return view != NULL && widget_view_is_container(view);
}

bool
widget_is_container(const struct widget *widget)
{
    const struct widget_view *view = widget_get_transformation_view(widget);
    return view != NULL && widget_view_is_container(view);
}

struct widget *
widget_get_child(struct widget *widget, const char *id)
{
    struct widget *child;

    assert(widget != NULL);
    assert(id != NULL);

    for (child = (struct widget *)widget->children.next;
         child != (struct widget *)&widget->children;
         child = (struct widget *)child->siblings.next) {
        if (child->id != NULL && strcmp(child->id, id) == 0)
            return child;
    }

    return NULL;
}

static bool
widget_check_untrusted_host(const struct widget *widget, const char *host)
{
    assert(widget->class != NULL);

    if (widget->class->untrusted_host == NULL)
        /* trusted widget is only allowed on a trusted host name
           (host==NULL) */
        return host == NULL;

    if (host == NULL)
        /* untrusted widget not allowed on trusted host name */
        return false;

    /* untrusted widget only allowed on matching untrusted host
       name */
    return strcmp(host, widget->class->untrusted_host) == 0;
}

static bool
widget_check_untrusted_prefix(const struct widget *widget, const char *host)
{
    assert(widget->class != NULL);

    if (widget->class->untrusted_prefix == NULL)
        /* trusted widget is only allowed on a trusted host name
           (host==NULL) */
        return host == NULL;

    if (host == NULL)
        /* untrusted widget not allowed on trusted host name */
        return false;

    /* untrusted widget only allowed on matching untrusted host
       name */
    size_t length = strlen(widget->class->untrusted_prefix);
    return memcmp(host, widget->class->untrusted_prefix, length) == 0 &&
        host[length] == '.';
}

static bool
widget_check_untrusted_site_suffix(const struct widget *widget,
                                   const char *host, const char *site_name)
{
    assert(widget->class != NULL);

    if (widget->class->untrusted_site_suffix == NULL)
        /* trusted widget is only allowed on a trusted host name
           (host==NULL) */
        return host == NULL;

    if (host == NULL || site_name == NULL)
        /* untrusted widget not allowed on trusted host name */
        return false;

    size_t site_name_length = strlen(site_name);
    return memcmp(host, site_name, site_name_length) == 0 &&
        host[site_name_length] == '.' &&
        strcmp(host + site_name_length + 1,
               widget->class->untrusted_site_suffix) == 0;
}

bool
widget_check_host(const struct widget *widget, const char *host,
                  const char *site_name)
{
    assert(widget->class != NULL);

    if (widget->class->untrusted_host != NULL)
        return widget_check_untrusted_host(widget, host);
    else if (widget->class->untrusted_prefix != NULL)
        return widget_check_untrusted_prefix(widget, host);
    else if (widget->class->untrusted_site_suffix != NULL)
        return widget_check_untrusted_site_suffix(widget, host, site_name);
    else
        /* trusted widget is only allowed on a trusted host name
           (host==NULL) */
        return host == NULL;
}

bool
widget_check_recursion(const struct widget *widget)
{
    unsigned depth = 0;

    assert(widget != NULL);

    do {
        if (++depth >= 8)
            return true;

        widget = widget->parent;
    } while (widget != NULL);

    return false;
}

void
widget_cancel(struct widget *widget)
{
    if (widget->from_request.body != NULL)
        /* we are not going to consume the request body, so abort
           it */
        istream_free_unused(&widget->from_request.body);

    if (widget->for_focused.body != NULL)
        /* the request body was not forwarded to the focused widget,
           so discard it */
        istream_free_unused(&widget->for_focused.body);
}
