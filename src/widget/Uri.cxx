/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Widget.hxx"
#include "Class.hxx"
#include "uri/uri_parser.hxx"
#include "puri_edit.hxx"
#include "puri_relative.hxx"
#include "args.hxx"
#include "tpool.hxx"
#include "http_address.hxx"
#include "lhttp_address.hxx"
#include "cgi_address.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

#include <assert.h>

/**
 * Returns the "base" address of the widget, i.e. without the widget
 * parameters from the parent container.
 */
ResourceAddress
Widget::GetBaseAddress(struct pool &_pool, bool stateful) const
{
    const ResourceAddress *src = stateful
        ? GetAddress()
        : GetStatelessAddress();

    if (!src->IsHttp() || from_template.query_string == nullptr)
        return {ShallowCopy(), *src};

    const auto &src_http = src->GetHttp();
    const char *const src_path = src_http.path;

    const char *uri = uri_delete_query_string(&_pool, src_path,
                                              from_template.query_string);

    if (!from_request.query_string.IsEmpty())
        uri = uri_delete_query_string(&_pool, src_path,
                                      from_request.query_string);

    if (uri == src_path)
        return {ShallowCopy(), *src};

    return src->WithPath(_pool, uri);
}

static const ResourceAddress *
widget_get_original_address(const Widget *widget)
{
    assert(widget != nullptr);
    assert(widget->cls != nullptr);

    const WidgetView *view = widget->GetAddressView();
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
Widget::DetermineAddress(bool stateful) const
{
    const char *path_info, *uri;
    ResourceAddress *address;

    assert(cls != nullptr);

    path_info = GetPathInfo(stateful);
    assert(path_info != nullptr);

    const auto *original_address = widget_get_original_address(this);
    if ((!stateful || from_request.query_string.IsEmpty()) &&
        *path_info == 0 &&
        from_template.query_string == nullptr)
        return original_address;

    switch (original_address->type) {
        CgiAddress *cgi;

    case ResourceAddress::Type::NONE:
    case ResourceAddress::Type::LOCAL:
    case ResourceAddress::Type::PIPE:
    case ResourceAddress::Type::NFS:
        break;

    case ResourceAddress::Type::HTTP:
        assert(original_address->GetHttp().path != nullptr);

        uri = original_address->GetHttp().path;

        if (*path_info != 0) {
            if (*path_info == '/' && HasTrailingSlash(uri))
                /* avoid generating a double slash when concatenating
                   URI path and path_info */
                ++path_info;

            uri = p_strcat(&pool, uri, path_info, nullptr);
        }

        if (from_template.query_string != nullptr)
            uri = uri_insert_query_string(&pool, uri,
                                          from_template.query_string);

        if (stateful && !from_request.query_string.IsNull())
            uri = uri_append_query_string_n(&pool, uri,
                                            from_request.query_string);

        return NewFromPool<ResourceAddress>(pool, original_address->WithPath(pool, uri));

    case ResourceAddress::Type::LHTTP:
        assert(original_address->GetLhttp().uri != nullptr);

        uri = original_address->GetLhttp().uri;

        if (*path_info != 0) {
            if (*path_info == '/' && HasTrailingSlash(uri))
                /* avoid generating a double slash when concatenating
                   URI path and path_info */
                ++path_info;

            uri = p_strcat(&pool, uri, path_info, nullptr);
        }

        if (from_template.query_string != nullptr)
            uri = uri_insert_query_string(&pool, uri,
                                          from_template.query_string);

        if (stateful && !from_request.query_string.IsNull())
            uri = uri_append_query_string_n(&pool, uri,
                                            from_request.query_string);

        return NewFromPool<ResourceAddress>(pool, original_address->WithPath(pool, uri));

    case ResourceAddress::Type::CGI:
    case ResourceAddress::Type::FASTCGI:
    case ResourceAddress::Type::WAS:
        address = original_address->Dup(pool);
        cgi = &address->GetCgi();

        if (*path_info != 0)
            cgi->path_info = cgi->path_info != nullptr
                ? uri_absolute(pool, cgi->path_info, path_info)
                : path_info;

        if (!stateful || from_request.query_string.IsEmpty())
            cgi->query_string = from_template.query_string;
        else if (from_template.query_string == nullptr)
            cgi->query_string = p_strdup(pool, from_request.query_string);
        else
            cgi->query_string =
                p_strncat(&pool,
                          from_request.query_string.data,
                          from_request.query_string.size,
                          "&", (size_t)1,
                          from_template.query_string,
                          strlen(from_template.query_string),
                          nullptr);

        return address;
    }

    return original_address;
}

const char *
Widget::AbsoluteUri(struct pool &_pool, bool stateful,
                    StringView relative_uri) const
{
    assert(GetAddress()->IsHttp());

    if (relative_uri.StartsWith({"~/", 2})) {
        relative_uri.skip_front(2);
        stateful = false;
    } else if (!relative_uri.IsEmpty() && relative_uri.front() == '/' &&
               cls != nullptr && cls->anchor_absolute) {
        relative_uri.skip_front(1);
        stateful = false;
    }

    const auto *uwa =
        &(stateful
          ? GetAddress()
          : GetStatelessAddress())->GetHttp();
    const char *base = uwa->path;
    if (relative_uri.IsNull())
        return uwa->GetAbsoluteURI(&_pool);

    const char *uri = uri_absolute(_pool, base, relative_uri);
    assert(uri != nullptr);
    if (!relative_uri.IsEmpty() &&
        from_template.query_string != nullptr)
        /* the relative_uri is non-empty, and uri_absolute() has
           removed the query string: re-add the configured query
           string */
        uri = uri_insert_query_string(&_pool, uri,
                                      from_template.query_string);

    return uwa->GetAbsoluteURI(&_pool, uri);
}

StringView
Widget::RelativeUri(struct pool &_pool, bool stateful,
                    StringView relative_uri) const
{
    const ResourceAddress *base;
    if (relative_uri.size >= 2 && relative_uri[0] == '~' &&
        relative_uri[1] == '/') {
        relative_uri.skip_front(2);
        base = widget_get_original_address(this);
    } else if (relative_uri.size >= 1 && relative_uri[0] == '/' &&
               cls != nullptr && cls->anchor_absolute) {
        relative_uri.skip_front(1);
        base = widget_get_original_address(this);
    } else
        base = NewFromPool<ResourceAddress>(_pool, GetBaseAddress(_pool, stateful));

    const auto address = base->Apply(_pool, relative_uri);
    if (!address.IsDefined())
        return nullptr;

    const auto *original_address = widget_get_original_address(this);
    return address.RelativeTo(*original_address);
}

/**
 * Returns true when the widget has the specified widget path.
 *
 * @param other the path to compare with; may be nullptr (i.e. never
 * matches)
 */
gcc_pure
static bool
compare_widget_path(const Widget *widget, const char *other)
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
Widget::ExternalUri(struct pool &_pool,
                    const struct parsed_uri *external_uri,
                    StringMap *args,
                    bool stateful,
                    StringView relative_uri,
                    const char *frame, const char *view) const
{
    const char *qmark, *args2, *new_uri;
    StringView p;

    const char *path = GetIdPath();
    if (path == nullptr ||
        external_uri == nullptr ||
        cls == &root_widget_class)
        return nullptr;

    const AutoRewindPool auto_rewind(*tpool);

    if (!relative_uri.IsNull()) {
        p = RelativeUri(*tpool, stateful, relative_uri);
        if (p.IsNull())
            return nullptr;
    } else
        p = nullptr;

    if (!p.IsNull() && relative_uri.Find('?') == nullptr &&
        from_template.query_string != nullptr) {
        /* no query string in relative_uri: if there is one in the new
           URI, check it and remove the configured parameters */
        const char *uri =
            uri_delete_query_string(tpool, p_strdup(*tpool, p),
                                    from_template.query_string);
        p = uri;
    }

    StringView query_string;
    if (!p.IsNull() && (qmark = p.Find('?')) != nullptr) {
        /* separate query_string from path_info */
        query_string = { qmark, p.end() };
        p.size = qmark - p.data;
    } else {
        query_string = nullptr;
    }

    StringView suffix;
    if (!p.IsNull() && cls->direct_addressing &&
        compare_widget_path(this, frame)) {
        /* new-style direct URI addressing: append */
        suffix = p;
        p = nullptr;
    } else
        suffix = "";

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format_n(tpool, args,
                          "focus", path,
                          p.IsNull() ? nullptr : "path", p,
                          frame == nullptr ? nullptr : "frame", frame,
                          nullptr);

    new_uri = p_strncat(&_pool,
                        external_uri->base.data,
                        external_uri->base.size,
                        ";", (size_t)1,
                        args2, strlen(args2),
                        "&view=", (size_t)(view != nullptr ? 6 : 0),
                        view != nullptr ? view : "",
                        view != nullptr ? strlen(view) : (size_t)0,
                        "/", (size_t)(suffix.size > 0),
                        suffix.data, suffix.size,
                        query_string.data, query_string.size,
                        nullptr);
    return new_uri;
}
