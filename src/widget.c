/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "strref-pool.h"

#include <string.h>
#include <assert.h>

void
widget_set_id(struct widget *widget, pool_t pool, const struct strref *id)
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
        widget->lazy.prefix = p_strcat(pool, p, widget->id, "__", NULL);
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

bool
widget_check_host(const struct widget *widget, const char *host)
{
    assert(widget->class != NULL);

    if (widget->class->untrusted_host != NULL)
        return widget_check_untrusted_host(widget, host);
    else if (widget->class->untrusted_prefix != NULL)
        return widget_check_untrusted_prefix(widget, host);
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
}
