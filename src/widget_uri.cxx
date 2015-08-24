/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"
#include "uri_relative.hxx"
#include "uri_parser.hxx"
#include "uri_edit.hxx"
#include "args.hxx"
#include "tpool.hxx"
#include "strref.h"
#include "strref_pool.hxx"
#include "http_address.hxx"
#include "lhttp_address.hxx"
#include "cgi_address.hxx"

#include <assert.h>

/**
 * Returns the "base" address of the widget, i.e. without the widget
 * parameters from the parent container.
 */
static const ResourceAddress *
widget_base_address(struct pool *pool, struct widget *widget, bool stateful)
{
    const ResourceAddress *src = stateful
        ? widget_address(widget) : widget_stateless_address(widget);
    const char *uri;

    if (src->type != ResourceAddress::Type::HTTP ||
        widget->query_string == nullptr)
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

    return src->DupWithPath(*pool, uri);
}

static const ResourceAddress *
widget_get_original_address(const struct widget *widget)
{
    assert(widget != nullptr);
    assert(widget->cls != nullptr);

    const WidgetView *view = widget_get_address_view(widget);
    assert(view != nullptr);

    return &view->address;
}

gcc_pure
static bool
HasTrailingSlash(const char *p)
{
    size_t length = strlen(p);
    return length > 0 && p[length - 1] == '/';
}

const ResourceAddress *
widget_determine_address(const struct widget *widget, bool stateful)
{
    struct pool *pool = widget->pool;
    const char *path_info, *uri;
    ResourceAddress *address;

    assert(widget != nullptr);
    assert(widget->cls != nullptr);

    path_info = stateful ? widget_get_path_info(widget) : widget->path_info;
    assert(path_info != nullptr);

    const ResourceAddress *original_address =
        widget_get_original_address(widget);
    switch (original_address->type) {
        struct cgi_address *cgi;

    case ResourceAddress::Type::NONE:
    case ResourceAddress::Type::LOCAL:
    case ResourceAddress::Type::PIPE:
    case ResourceAddress::Type::NFS:
        break;

    case ResourceAddress::Type::HTTP:
    case ResourceAddress::Type::AJP:
        assert(original_address->u.http->path != nullptr);

        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == nullptr)
            break;

        uri = original_address->u.http->path;

        if (*path_info != 0) {
            if (*path_info == '/' && HasTrailingSlash(uri))
                /* avoid generating a double slash when concatenating
                   URI path and path_info */
                ++path_info;

            uri = p_strcat(pool, uri, path_info, nullptr);
        }

        if (widget->query_string != nullptr)
            uri = uri_insert_query_string(pool, uri,
                                          widget->query_string);

        if (stateful && !strref_is_empty(&widget->from_request.query_string))
            uri = uri_append_query_string_n(pool, uri,
                                            widget->from_request.query_string.data,
                                            widget->from_request.query_string.length);

        return original_address->DupWithPath(*pool, uri);

    case ResourceAddress::Type::LHTTP:
        assert(original_address->u.lhttp->uri != nullptr);

        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == nullptr)
            break;

        uri = original_address->u.lhttp->uri;


        if (*path_info != 0) {
            if (*path_info == '/' && HasTrailingSlash(uri))
                /* avoid generating a double slash when concatenating
                   URI path and path_info */
                ++path_info;

            uri = p_strcat(pool, uri, path_info, nullptr);
        }

        if (widget->query_string != nullptr)
            uri = uri_insert_query_string(pool, uri,
                                          widget->query_string);

        if (stateful && !strref_is_empty(&widget->from_request.query_string))
            uri = uri_append_query_string_n(pool, uri,
                                            widget->from_request.query_string.data,
                                            widget->from_request.query_string.length);

        return original_address->DupWithPath(*pool, uri);

    case ResourceAddress::Type::CGI:
    case ResourceAddress::Type::FASTCGI:
    case ResourceAddress::Type::WAS:
        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == nullptr)
            break;

        address = original_address->Dup(*pool);
        cgi = address->GetCgi();

        if (*path_info != 0)
            cgi->path_info = cgi->path_info != nullptr
                ? uri_absolute(pool, cgi->path_info,
                               path_info, strlen(path_info))
                : path_info;

        if (!stateful)
            cgi->query_string = widget->query_string;
        else if (strref_is_empty(&widget->from_request.query_string))
            cgi->query_string = widget->query_string;
        else if (widget->query_string == nullptr)
            cgi->query_string =
                strref_dup(pool, &widget->from_request.query_string);
        else
            cgi->query_string =
                p_strncat(pool,
                          widget->from_request.query_string.data,
                          widget->from_request.query_string.length,
                          "&", (size_t)1,
                          widget->query_string, strlen(widget->query_string),
                          nullptr);

        return address;
    }

    return original_address;
}

const char *
widget_absolute_uri(struct pool *pool, struct widget *widget, bool stateful,
                    const struct strref *relative_uri)
{
    assert(widget_address(widget)->type == ResourceAddress::Type::HTTP);

    struct strref buffer;
    if (relative_uri != nullptr && strref_starts_with_n(relative_uri, "~/", 2)) {
        buffer = *relative_uri;
        strref_skip(&buffer, 2);
        relative_uri = &buffer;
        stateful = false;
    } else if (relative_uri != nullptr &&
               strref_starts_with_n(relative_uri, "/", 1) &&
               widget->cls != nullptr && widget->cls->anchor_absolute) {
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
    if (relative_uri == nullptr)
        return uwa->GetAbsoluteURI(pool);

    const char *uri = uri_absolute(pool, base, relative_uri->data,
                                   relative_uri->length);
    assert(uri != nullptr);
    if (!strref_is_empty(relative_uri) && widget->query_string != nullptr)
        /* the relative_uri is non-empty, and uri_absolute() has
           removed the query string: re-add the configured query
           string */
        uri = uri_insert_query_string(pool, uri, widget->query_string);

    return uwa->GetAbsoluteURI(pool, uri);
}

const struct strref *
widget_relative_uri(struct pool *pool, struct widget *widget, bool stateful,
                    const char *relative_uri, size_t relative_uri_length,
                    struct strref *buffer)
{
    const ResourceAddress *base;
    if (relative_uri_length >= 2 && relative_uri[0] == '~' &&
        relative_uri[1] == '/') {
        relative_uri += 2;
        relative_uri_length -= 2;
        base = widget_get_original_address(widget);
    } else if (relative_uri_length >= 1 && relative_uri[0] == '/' &&
               widget->cls != nullptr && widget->cls->anchor_absolute) {
        relative_uri += 1;
        relative_uri_length -= 1;
        base = widget_get_original_address(widget);
    } else
        base = widget_base_address(pool, widget, stateful);

    ResourceAddress address_buffer;
    const auto address = base->Apply(*pool, relative_uri, relative_uri_length,
                                     address_buffer);
    if (address == nullptr)
        return nullptr;

    const ResourceAddress *original_address =
        widget_get_original_address(widget);
    return address->RelativeTo(*original_address, *buffer);
}

/**
 * Returns true when the widget has the specified widget path.
 *
 * @param other the path to compare with; may be nullptr (i.e. never
 * matches)
 */
gcc_pure
static bool
compare_widget_path(const struct widget *widget, const char *other)
{
    assert(widget != nullptr);

    if (other == nullptr)
        return false;

    const char *path = widget->GetIdPath();
    if (path == nullptr)
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
    const char *qmark, *args2, *new_uri;
    struct strref buffer, query_string;
    const struct strref *p;
    struct pool_mark_state mark;

    const char *path = widget->GetIdPath();
    if (path == nullptr ||
        external_uri == nullptr ||
        widget->cls == &root_widget_class)
        return nullptr;

    pool_mark(tpool, &mark);

    if (relative_uri != nullptr) {
        p = widget_relative_uri(tpool, widget, stateful,
                                relative_uri->data, relative_uri->length,
                                &buffer);
        if (p == nullptr) {
            pool_rewind(tpool, &mark);
            return nullptr;
        }
    } else
        p = nullptr;

    if (p != nullptr && strref_chr(relative_uri, '?') == 0 &&
        widget->query_string != nullptr) {
        /* no query string in relative_uri: if there is one in the new
           URI, check it and remove the configured parameters */
        const char *uri =
            uri_delete_query_string(tpool, strref_dup(tpool, p),
                                    widget->query_string,
                                    strlen(widget->query_string));
        strref_set_c(&buffer, uri);
        p = &buffer;
    }

    if (p != nullptr &&
        (qmark = (const char *)memchr(p->data, '?', p->length)) != nullptr) {
        /* separate query_string from path_info */
        strref_set2(&query_string, qmark, strref_end(p));
        strref_set2(&buffer, p->data, qmark);
        p = &buffer;
    } else {
        strref_null(&query_string);
    }

    struct strref suffix;
    if (p != nullptr && widget->cls->direct_addressing &&
        compare_widget_path(widget, frame)) {
        /* new-style direct URI addressing: append */
        suffix = *p;
        p = nullptr;
    } else
        strref_set_empty(&suffix);

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format_n(tpool, args,
                          "focus", path, strlen(path),
                          p == nullptr ? nullptr : "path",
                          p == nullptr ? nullptr : p->data,
                          p == nullptr ? (size_t)0 : p->length,
                          frame == nullptr ? nullptr : "frame", frame,
                          frame == nullptr ? 0 : strlen(frame),
                          nullptr);

    new_uri = p_strncat(pool,
                        external_uri->base.data,
                        external_uri->base.length,
                        ";", (size_t)1,
                        args2, strlen(args2),
                        "&view=", (size_t)(view != nullptr ? 6 : 0),
                        view != nullptr ? view : "",
                        view != nullptr ? strlen(view) : (size_t)0,
                        "/", (size_t)(suffix.length > 0),
                        suffix.data, suffix.length,
                        query_string.data, query_string.length,
                        nullptr);

    pool_rewind(tpool, &mark);
    return new_uri;
}
