/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "widget-class.h"
#include "uri-relative.h"
#include "uri-parser.h"
#include "uri-edit.h"
#include "args.h"
#include "tpool.h"
#include "strref.h"
#include "strref-pool.h"
#include "http_address.h"
#include "lhttp_address.h"

#include <assert.h>

/**
 * Returns the "base" address of the widget, i.e. without the widget
 * parameters from the parent container.
 */
static const struct resource_address *
widget_base_address(struct pool *pool, struct widget *widget, bool stateful)
{
    const struct resource_address *src = stateful
        ? widget_address(widget) : widget_stateless_address(widget);
    const char *uri;

    if (src->type != RESOURCE_ADDRESS_HTTP || widget->query_string == NULL)
        return src;

    uri = uri_delete_query_string(pool, src->u.http->path,
                                  widget->query_string,
                                  strlen(widget->query_string));

    if (!strref_is_empty(&widget->from_request.query_string))
        uri = uri_delete_query_string(pool, src->u.http->path,
                                      widget->from_request.query_string.data,
                                      widget->from_request.query_string.length);

    if (uri == src->u.http->path)
        return src;

    return resource_address_dup_with_path(pool, src, uri);
}

static const struct resource_address *
widget_get_original_address(const struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->cls != NULL);

    const struct widget_view *view = widget_get_address_view(widget);
    assert(view != NULL);

    return &view->address;
}

const struct resource_address *
widget_determine_address(const struct widget *widget, bool stateful)
{
    struct pool *pool = widget->pool;
    const char *path_info, *uri;
    struct resource_address *address;

    assert(widget != NULL);
    assert(widget->cls != NULL);

    path_info = stateful ? widget_get_path_info(widget) : widget->path_info;
    assert(path_info != NULL);

    const struct resource_address *original_address =
        widget_get_original_address(widget);
    switch (original_address->type) {
        struct cgi_address *cgi;

    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(original_address->u.http->path != NULL);

        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        uri = original_address->u.http->path;

        if (*path_info != 0)
            uri = p_strcat(pool, uri, path_info, NULL);

        if (widget->query_string != NULL)
            uri = uri_insert_query_string(pool, uri,
                                          widget->query_string);

        if (stateful && !strref_is_empty(&widget->from_request.query_string))
            uri = uri_append_query_string_n(pool, uri,
                                            widget->from_request.query_string.data,
                                            widget->from_request.query_string.length);

        return resource_address_dup_with_path(pool, original_address, uri);

    case RESOURCE_ADDRESS_LHTTP:
        assert(original_address->u.lhttp->uri != NULL);

        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        uri = original_address->u.lhttp->uri;

        if (*path_info != 0)
            uri = p_strcat(pool, uri, path_info, NULL);

        if (widget->query_string != NULL)
            uri = uri_insert_query_string(pool, uri,
                                          widget->query_string);

        if (stateful && !strref_is_empty(&widget->from_request.query_string))
            uri = uri_append_query_string_n(pool, uri,
                                            widget->from_request.query_string.data,
                                            widget->from_request.query_string.length);

        return resource_address_dup_with_path(pool, original_address, uri);

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        address = resource_address_dup(pool, original_address);
        cgi = resource_address_get_cgi(address);

        if (*path_info != 0)
            cgi->path_info = cgi->path_info != NULL
                ? uri_absolute(pool, cgi->path_info,
                               path_info, strlen(path_info))
                : path_info;

        if (!stateful)
            cgi->query_string = widget->query_string;
        else if (strref_is_empty(&widget->from_request.query_string))
            cgi->query_string = widget->query_string;
        else if (widget->query_string == NULL)
            cgi->query_string =
                strref_dup(pool, &widget->from_request.query_string);
        else
            cgi->query_string =
                p_strncat(pool,
                          widget->from_request.query_string.data,
                          widget->from_request.query_string.length,
                          "&", (size_t)1,
                          widget->query_string, strlen(widget->query_string),
                          NULL);

        return address;
    }

    return original_address;
}

const char *
widget_absolute_uri(struct pool *pool, struct widget *widget, bool stateful,
                    const struct strref *relative_uri)
{
    assert(widget_address(widget)->type == RESOURCE_ADDRESS_HTTP);

    struct strref buffer;
    if (relative_uri != NULL && strref_starts_with_n(relative_uri, "~/", 2)) {
        buffer = *relative_uri;
        strref_skip(&buffer, 2);
        relative_uri = &buffer;
        stateful = false;
    } else if (relative_uri != NULL &&
               strref_starts_with_n(relative_uri, "/", 1) &&
               widget->cls != NULL && widget->cls->anchor_absolute) {
        buffer = *relative_uri;
        strref_skip(&buffer, 1);
        relative_uri = &buffer;
        stateful = false;
    }

    const struct http_address *uwa =
        (stateful
         ? widget_address(widget)
         : widget_stateless_address(widget))->u.http;
    const char *base = uwa->path;
    if (relative_uri == NULL)
        return http_address_absolute(pool, uwa);

    const char *uri = uri_absolute(pool, base, relative_uri->data,
                                   relative_uri->length);
    assert(uri != NULL);
    if (!strref_is_empty(relative_uri) && widget->query_string != NULL)
        /* the relative_uri is non-empty, and uri_absolute() has
           removed the query string: re-add the configured query
           string */
        uri = uri_insert_query_string(pool, uri, widget->query_string);

    return http_address_absolute_with_path(pool, uwa, uri);
}

const struct strref *
widget_relative_uri(struct pool *pool, struct widget *widget, bool stateful,
                    const char *relative_uri, size_t relative_uri_length,
                    struct strref *buffer)
{
    const struct resource_address *base;
    if (relative_uri_length >= 2 && relative_uri[0] == '~' &&
        relative_uri[1] == '/') {
        relative_uri += 2;
        relative_uri_length -= 2;
        base = widget_get_original_address(widget);
    } else if (relative_uri_length >= 1 && relative_uri[0] == '/' &&
               widget->cls != NULL && widget->cls->anchor_absolute) {
        relative_uri += 1;
        relative_uri_length -= 1;
        base = widget_get_original_address(widget);
    } else
        base = widget_base_address(pool, widget, stateful);

    struct resource_address address_buffer;
    const struct resource_address *address;

    address = resource_address_apply(pool, base,
                                     relative_uri, relative_uri_length,
                                     &address_buffer);
    if (address == NULL)
        return NULL;

    const struct resource_address *original_address =
        widget_get_original_address(widget);
    return resource_address_relative(original_address, address, buffer);
}

/**
 * Returns true when the widget has the specified widget path.
 *
 * @param other the path to compare with; may be NULL (i.e. never
 * matches)
 */
gcc_pure
static bool
compare_widget_path(const struct widget *widget, const char *other)
{
    assert(widget != NULL);

    if (other == NULL)
        return false;

    const char *path = widget_path(widget);
    if (path == NULL)
        return false;

    return strcmp(path, other) == 0;
}

const char *
widget_external_uri(struct pool *pool,
                    const struct parsed_uri *external_uri,
                    struct strmap *args,
                    struct widget *widget, bool stateful,
                    const struct strref *relative_uri,
                    const char *frame, const char *view)
{
    const char *path;
    const char *qmark, *args2, *new_uri;
    struct strref buffer, query_string;
    const struct strref *p;
    struct pool_mark_state mark;

    path = widget_path(widget);
    if (path == NULL ||
        external_uri == NULL ||
        widget->cls == &root_widget_class)
        return NULL;

    pool_mark(tpool, &mark);

    if (relative_uri != NULL) {
        p = widget_relative_uri(tpool, widget, stateful,
                                relative_uri->data, relative_uri->length,
                                &buffer);
        if (p == NULL) {
            pool_rewind(tpool, &mark);
            return NULL;
        }
    } else
        p = NULL;

    if (p != NULL && strref_chr(relative_uri, '?') == 0 &&
        widget->query_string != NULL) {
        /* no query string in relative_uri: if there is one in the new
           URI, check it and remove the configured parameters */
        const char *uri =
            uri_delete_query_string(tpool, strref_dup(tpool, p),
                                    widget->query_string,
                                    strlen(widget->query_string));
        strref_set_c(&buffer, uri);
        p = &buffer;
    }

    if (p != NULL && (qmark = memchr(p->data, '?', p->length)) != NULL) {
        /* separate query_string from path_info */
        strref_set2(&query_string, qmark, strref_end(p));
        strref_set2(&buffer, p->data, qmark);
        p = &buffer;
    } else {
        strref_null(&query_string);
    }

    struct strref suffix;
    if (p != NULL && widget->cls->direct_addressing &&
        compare_widget_path(widget, frame)) {
        /* new-style direct URI addressing: append */
        suffix = *p;
        p = NULL;
    } else
        strref_set_empty(&suffix);

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format_n(tpool, args,
                          "focus", path, strlen(path),
                          p == NULL ? NULL : "path",
                          p == NULL ? NULL : p->data,
                          p == NULL ? (size_t)0 : p->length,
                          frame == NULL ? NULL : "frame", frame,
                          frame == NULL ? 0 : strlen(frame),
                          NULL);

    new_uri = p_strncat(pool,
                        external_uri->base.data,
                        external_uri->base.length,
                        ";", (size_t)1,
                        args2, strlen(args2),
                        "&view=", (size_t)(view != NULL ? 6 : 0),
                        view != NULL ? view : "",
                        view != NULL ? strlen(view) : (size_t)0,
                        "/", (size_t)(suffix.length > 0),
                        suffix.data, suffix.length,
                        query_string.data, query_string.length,
                        NULL);

    pool_rewind(tpool, &mark);
    return new_uri;
}
