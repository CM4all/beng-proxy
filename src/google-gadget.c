/*
 * Emulation layer for Google gadgets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "google-gadget-internal.h"
#include "widget.h"
#include "istream-internal.h"
#include "http-cache.h"
#include "http-response.h"
#include "parser.h"
#include "processor.h"
#include "growing-buffer.h"
#include "uri-address.h"
#include "session.h"
#include "strref-pool.h"

/* XXX make the URL configurable */
static const char jscript[] =
    "<script type=\"text/javascript\" src=\"/cm4all-beng-proxy/google-gadget.js\"></script>\n";

static istream_t
google_gadget_process(struct google_gadget *gg, istream_t istream)
{
    const char *prefix;

    assert(gg != NULL);
    assert(gg->delayed != NULL);
    assert(gg->subst != NULL);

    istream_delayed_set(gg->delayed, istream);
    gg->delayed = NULL;

    istream = gg->subst;

    prefix = widget_prefix(gg->widget);
    if (prefix != NULL) {
        const char *module_id = p_strcat(gg->pool, prefix, "widget", NULL);
        istream_subst_add(istream, "__MODULE_ID__", module_id);
    }

    istream_subst_add(istream, "__BIDI_START_EDGE__", "left");
    istream_subst_add(istream, "__BIDI_END_EDGE__", "right");

    return istream;
}

static void
gg_set_content(struct google_gadget *gg, istream_t istream)
{
    http_status_t status;
    struct strmap *headers;

    assert(gg != NULL);

    if (gg->has_locale && gg->waiting_for_locale) {
            /* XXX abort locale */
    }

    if (istream == NULL) {
        status = HTTP_STATUS_NO_CONTENT;
        headers = NULL;
    } else {
        status = HTTP_STATUS_OK;
        headers = strmap_new(gg->pool, 4);
        strmap_add(headers, "content-type", "text/html; charset=utf-8");
    }

    if (gg->delayed != NULL) {
        gg->delayed = NULL;
        istream_free(&gg->subst);
    }

    http_response_handler_invoke_response(&gg->response_handler,
                                          status, headers, istream);
}

static void
google_send_error(struct google_gadget *gg, const char *msg)
{
    struct strmap *headers = strmap_new(gg->pool, 4);
    istream_t response = istream_string_new(gg->pool, msg);

    gg->delayed = NULL;
    istream_free(&gg->subst);

    strmap_add(headers, "content-type", "text/plain");
    http_response_handler_invoke_response(&gg->response_handler,
                                          HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                          headers, response);

    if (gg->parser != NULL)
        parser_close(gg->parser);
    else if (async_ref_defined(&gg->async))
        async_abort(&gg->async);
}


/*
 * istream implementation which serves the CDATA section in <Content/>
 *
 */

static inline struct google_gadget *
istream_to_google_gadget(istream_t istream)
{
    return (struct google_gadget *)(((char*)istream) - offsetof(struct google_gadget, output));
}

static void
istream_google_html_read(istream_t istream)
{
    struct google_gadget *gg = istream_to_google_gadget(istream);

    assert(gg->parser != NULL);
    assert(gg->from_parser.sending_content);

    if ((!gg->has_locale || !gg->waiting_for_locale) &&
        !gg->from_parser.in_parser)
        parser_read(gg->parser);
}

static void
istream_google_html_close(istream_t istream)
{
    struct google_gadget *gg = istream_to_google_gadget(istream);

    assert(gg->parser != NULL);
    assert(gg->from_parser.sending_content);

    parser_close(gg->parser);

    istream_deinit_abort(&gg->output);
    pool_unref(gg->pool);
}

static const struct istream istream_google_html = {
    .read = istream_google_html_read,
    .close = istream_google_html_close,
};


/*
 * msg callbacks
 *
 */

void
google_gadget_msg_eof(struct google_gadget *gg)
{
    /* XXX */

    assert(gg->has_locale && gg->waiting_for_locale);

    gg->waiting_for_locale = false;

    if (gg->parser != NULL && !gg->from_parser.in_parser)
        parser_read(gg->parser);
}

void
google_gadget_msg_abort(struct google_gadget *gg)
{
    /* XXX */
    google_gadget_msg_eof(gg);
}


/*
 * produce output
 *
 */

static void
growing_buffer_escape_string(struct growing_buffer *gb, const char *src0)
{
    size_t length = 0;
    const char *src;
    char *dest;

    for (src = src0; *src != 0; ++src) {
        if (*src == '"')
            length += 2;
        else if ((signed char)*src >= 0x20)
            ++length;
    }

    if (length == 0)
        return;

    dest = growing_buffer_write(gb, length);
    for (src = src0; *src != 0; ++src) {
        if (*src == '"') {
            *dest++ = '\\';
            *dest++ = *src;
        } else if ((signed char)*src >= 0x20)
            *dest++ = *src;
    }
}

static void
growing_buffer_write_jscript_string(struct growing_buffer *gb, const char *s)
{
    if (s == NULL)
        growing_buffer_write_string(gb, "null");
    else {
        growing_buffer_write_string(gb, "\"");
        growing_buffer_escape_string(gb, s);
        growing_buffer_write_string(gb, "\"");
    }
}

static istream_t
generate_jscript(pool_t pool, struct widget *widget, struct session *session2)
{
    struct growing_buffer *gb = growing_buffer_new(pool, 256);
    struct widget_session *session;

    growing_buffer_write_string(gb, "<script type=\"text/javascript\">\n"
                                "var _beng_proxy_widget_path = ");
    growing_buffer_write_jscript_string(gb, widget_path(widget));
    growing_buffer_write_string(gb, ";\n"
                                "var _beng_proxy_widget_prefs = ");

    lock_lock(&session2->lock);
    session = widget_get_session(widget, session2, false);
    growing_buffer_write_jscript_string(gb, session == NULL ? NULL : session->query_string);
    lock_unlock(&session2->lock);

    growing_buffer_write_string(gb, ";\n");

    growing_buffer_write_string(gb, "</script>\n");

    growing_buffer_write_string(gb, jscript);

    return growing_buffer_istream(gb);
}

static istream_t
generate_iframe(pool_t pool, const char *uri)
{
    struct growing_buffer *gb;

    assert(uri != NULL);

    gb = growing_buffer_new(pool, 512);
    growing_buffer_write_string(gb, "<iframe "
                                "width='100%' height='100%' "
                                "frameborder='0' marginheight='0' marginwidth='0' "
                                "scrolling='no' "
                                "src='");
    growing_buffer_write_string(gb, uri);
    growing_buffer_write_string(gb, "'></iframe>");

    return growing_buffer_istream(gb);
}

static void
google_content_tag_finished(struct google_gadget *gg,
                            const struct parser_tag *tag)
{
    istream_t istream;

    switch (gg->from_parser.type) {
    case TYPE_NONE:
        break;

    case TYPE_HTML:
    case TYPE_HTML_INLINE:
        if (tag->type == TAG_OPEN) {
            if (gg->widget->from_request.proxy ||
                gg->from_parser.type == TYPE_HTML_INLINE) {
                gg->from_parser.sending_content = true;
                istream_init(&gg->output, &istream_google_html, gg->pool);

                istream = istream_struct_cast(&gg->output);
                istream = google_gadget_process(gg, istream);
                istream = istream_cat_new(gg->pool,
                                          generate_jscript(gg->pool, gg->widget,
                                                           session_get(gg->env->session_id)),
                                          istream,
                                          NULL);

                gg_set_content(gg, istream);
            } else {
                const char *uri =
                    widget_external_uri(gg->pool, gg->env->external_uri,
                                        gg->env->args,
                                        gg->widget, false, NULL, 0,
                                        widget_path(gg->widget), false);

                if (uri != NULL)
                    istream = generate_iframe(gg->pool, uri);
                else
                    istream = NULL;
                gg_set_content(gg, istream);

                if (gg->has_locale && gg->waiting_for_locale)
                    google_gadget_msg_close(gg);

                parser_close(gg->parser);
                pool_unref(gg->pool);
            }
        } else {
            /* it's TAG_SHORT, handle that gracefully */

            gg_set_content(gg, NULL);
        }

        return;

    case TYPE_URL:
        if (gg->from_parser.url == NULL)
            break;

        istream = generate_iframe(gg->pool, gg->from_parser.url);
        gg_set_content(gg, istream);

        if (gg->has_locale && gg->waiting_for_locale)
            google_gadget_msg_close(gg);

        parser_close(gg->parser);
        pool_unref(gg->pool);
        return;
    }

    google_send_error(gg, "malformed google gadget");
    pool_unref(gg->pool);
}


/*
 * parser callbacks
 *
 */

static void
google_parser_tag_start(const struct parser_tag *tag, void *ctx)
{
    struct google_gadget *gg = ctx;

    if (gg->from_parser.sending_content) {
        gg->from_parser.sending_content = false;
        istream_deinit_eof(&gg->output);
    }

    if (!gg->has_locale && tag->type != TAG_CLOSE &&
        strref_cmp_literal(&tag->name, "locale") == 0) {
        gg->from_parser.tag = TAG_LOCALE;
        gg->has_locale = true;
        gg->waiting_for_locale = false;
    } else if (strref_cmp_literal(&tag->name, "content") == 0) {
        gg->from_parser.tag = TAG_CONTENT;
    } else {
        gg->from_parser.tag = TAG_NONE;
    }
}

static void
google_parser_tag_finished(const struct parser_tag *tag, void *ctx)
{
    struct google_gadget *gg = ctx;

    gg->from_parser.in_parser = true;

    if (tag->type != TAG_CLOSE &&
        gg->from_parser.tag == TAG_CONTENT &&
        gg->delayed != NULL) {
        gg->from_parser.tag = TAG_NONE;
        google_content_tag_finished(gg, tag);
    } else {
        gg->from_parser.tag = TAG_NONE;
    }

    gg->from_parser.in_parser = false;
}

static void
google_parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    struct google_gadget *gg = ctx;

    gg->from_parser.in_parser = true;

    switch (gg->from_parser.tag) {
    case TAG_NONE:
        break;

    case TAG_LOCALE:
        if (strref_cmp_literal(&attr->name, "messages") == 0 &&
            !strref_is_empty(&attr->value) &&
            gg->delayed != NULL) {
            const char *url;

            gg->waiting_for_locale = true;

            url = widget_absolute_uri(gg->pool, gg->widget,
                                      attr->value.data, attr->value.length);
            if (url == NULL)
                url = strref_dup(gg->pool, &attr->value);
            google_gadget_msg_load(gg, url);
        }
        break;

    case TAG_CONTENT:
        if (strref_cmp_literal(&attr->name, "type") == 0) {
            if (strref_cmp_literal(&attr->value, "url") == 0) {
                gg->from_parser.type = TYPE_URL;
                gg->from_parser.url = NULL;
            } else if (strref_cmp_literal(&attr->value, "html") == 0)
                gg->from_parser.type = TYPE_HTML;
            else if (strref_cmp_literal(&attr->value, "html-inline") == 0)
                gg->from_parser.type = TYPE_HTML_INLINE;
            else {
                google_send_error(gg, "unknown type attribute");
                /* don't reset gg->from_parser.in_parser here because
                   the object is already destructed */
                pool_unref(gg->pool);
                return;
            }
        } else if (gg->from_parser.type == TYPE_URL &&
                   strref_cmp_literal(&attr->name, "href") == 0) {
            gg->from_parser.url = strref_dup(gg->pool, &attr->value);
        }

        break;
    }

    gg->from_parser.in_parser = false;
}

static size_t
google_parser_cdata(const char *p, size_t length, bool escaped, void *ctx)
{
    struct google_gadget *gg = ctx;

    if (!escaped && gg->from_parser.sending_content) {
        if (gg->has_locale && gg->waiting_for_locale)
            return 0;
        return istream_invoke_data(&gg->output, p, length);
    } else
        return length;
}

static void
google_parser_eof(void *ctx, off_t __attr_unused length)
{
    struct google_gadget *gg = ctx;

    gg->parser = NULL;

    if (gg->has_locale && gg->waiting_for_locale)
        google_gadget_msg_close(gg);

    if (gg->from_parser.sending_content) {
        gg->from_parser.sending_content = false;
        istream_deinit_eof(&gg->output);
    } else if (gg->delayed != NULL && !async_ref_defined(&gg->async))
        google_send_error(gg, "google gadget did not contain a valid Content element");

    pool_unref(gg->pool);
}

static void
google_parser_abort(void *ctx)
{
    struct google_gadget *gg = ctx;

    gg->parser = NULL;

    if (gg->has_locale && gg->waiting_for_locale)
        google_gadget_msg_close(gg);

    if (gg->from_parser.sending_content) {
        gg->from_parser.sending_content = false;
        istream_deinit_abort(&gg->output);
    } else if (gg->delayed != NULL)
        google_send_error(gg, "google gadget retrieval aborted");

    pool_unref(gg->pool);
}

static const struct parser_handler google_parser_handler = {
    .tag_start = google_parser_tag_start,
    .tag_finished = google_parser_tag_finished,
    .attr_finished = google_parser_attr_finished,
    .cdata = google_parser_cdata,
    .eof = google_parser_eof,
    .abort = google_parser_abort,
};


/*
 * url_stream handler (gadget description)
 *
 */

static void
google_gadget_http_response(http_status_t status, struct strmap *headers,
                            istream_t body, void *ctx)
{
    struct google_gadget *gg = ctx;
    const char *p;

    assert(gg->delayed != NULL);

    async_ref_clear(&gg->async);

    if (!http_status_is_success(status)) {
        if (body != NULL)
            istream_close(body);

        google_send_error(gg, "widget server reported error");
        pool_unref(gg->pool);
        return;
    }

    p = strmap_get(headers, "content-type");
    if (p == NULL || body == NULL ||
        (strncmp(p, "text/xml", 8) != 0 &&
         strncmp(p, "application/xml", 15) != 0)) {
        if (body != NULL)
            istream_close(body);

        google_send_error(gg, "text/xml expected");
        pool_unref(gg->pool);
        return;
    }

    gg->from_parser.tag = TAG_NONE;
    gg->from_parser.type = TYPE_NONE;
    gg->from_parser.sending_content = false;
    gg->from_parser.in_parser = false;
    gg->parser = parser_new(gg->pool, body,
                            &google_parser_handler, gg);
    istream_read(body);
}

static void
google_gadget_http_abort(void *ctx)
{
    struct google_gadget *gg = ctx;

    async_ref_clear(&gg->async);

    if (gg->delayed != NULL)
        istream_free(&gg->delayed);

    pool_unref(gg->pool);
}

static const struct http_response_handler google_gadget_handler = {
    .response = google_gadget_http_response,
    .abort = google_gadget_http_abort,
};


/*
 * async operation
 *
 */

static struct google_gadget *
async_to_gg(struct async_operation *ao)
{
    return (struct google_gadget*)(((char*)ao) - offsetof(struct google_gadget, async_operation));
}

static void
gg_async_abort(struct async_operation *ao)
{
    struct google_gadget *gg = async_to_gg(ao);

    assert((gg->delayed == NULL) == (gg->subst == NULL));

    if (gg->delayed == NULL)
        return;

    gg->delayed = NULL;
    istream_free(&gg->subst);

    if (gg->parser != NULL)
        parser_close(gg->parser);
    else if (async_ref_defined(&gg->async))
        async_abort(&gg->async);

    pool_unref(gg->pool);
}

static struct async_operation_class gg_async_operation = {
    .abort = gg_async_abort,
};


/*
 * constructor
 *
 */

void
embed_google_gadget(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct google_gadget *gg;

    assert(widget != NULL);
    assert(widget->class != NULL);

    if (widget->class->address.type != RESOURCE_ADDRESS_HTTP) {
        /* google gadgets must be served from a HTTP server */
        struct http_response_handler_ref handler_ref;
        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_abort(&handler_ref);
        return;
    }

    if (widget->from_request.proxy && strmap_get(env->args, "save") != NULL) {
        struct http_response_handler_ref handler_ref;
        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_NO_CONTENT,
                                              NULL, NULL);
        return;
    }

    pool_ref(pool);

    gg = p_malloc(pool, sizeof(*gg));
    gg->pool = pool;
    gg->env = env;
    gg->widget = widget;

    async_init(&gg->async_operation, &gg_async_operation);
    async_ref_set(async_ref, &gg->async_operation);

    gg->delayed = istream_delayed_new(pool);
    async_ref_clear(istream_delayed_async(gg->delayed));

    gg->subst = istream_subst_new(pool, gg->delayed);
    gg->parser = NULL;
    gg->has_locale = false;

    http_response_handler_set(&gg->response_handler, handler, handler_ctx);

    http_cache_request(env->http_cache, pool,
                       HTTP_METHOD_GET, widget->class->address.u.http,
                       NULL, NULL,
                       &google_gadget_handler, gg,
                       &gg->async);
}
