/*
 * Emulation layer for Google gadgets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "google-gadget-internal.h"
#include "widget.h"
#include "istream.h"
#include "url-stream.h"
#include "http-response.h"
#include "parser.h"
#include "processor.h"
#include "embed.h"

static const struct widget_class *
gg_class(pool_t pool, const char *uri)
{
    /* XXX merge this method with get_widget_class() */
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->uri = uri;
    wc->type = WIDGET_TYPE_GOOGLE_GADGET;
    wc->is_container = 0;

    return wc;
}

static void
google_gadget_process(struct google_gadget *gg, istream_t istream,
                      unsigned options)
{
    processor_new(gg->pool, istream,
                  gg->widget, gg->env,
                  options,
                  gg->response_handler.handler,
                  gg->response_handler.ctx,
                  &gg->async);
}

static void
gg_set_content(struct google_gadget *gg, istream_t istream, int process)
{
    http_status_t status;
    strmap_t headers;

    assert(gg != NULL);
    assert(gg->delayed != NULL);
    assert(gg->subst != NULL);

    if (gg->has_locale && gg->waiting_for_locale) {
            /* XXX abort locale */
    }

    if (istream == NULL) {
        status = HTTP_STATUS_NO_CONTENT;
        headers = NULL;
    } else {
        status = HTTP_STATUS_OK;
        headers = strmap_new(gg->pool, 4);
        strmap_addn(headers, "content-type", "text/html; charset=utf-8");

        if (process) {
            unsigned options = PROCESSOR_JSCRIPT|
                PROCESSOR_JSCRIPT_PREFS;

            if (gg->widget->from_request.proxy)
                options |= PROCESSOR_JSCRIPT_ROOT;

            istream_delayed_set(gg->delayed, istream);
            gg->delayed = NULL;
            google_gadget_process(gg, gg->subst, options);
            return;
        }
    }

    gg->delayed = NULL;
    istream_free(&gg->subst);

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

    strmap_addn(headers, "content-type", "text/plain");
    http_response_handler_invoke_response(&gg->response_handler,
                                          HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                          headers, response);

    if (gg->parser != NULL)
        parser_close(gg->parser);
    else if (async_ref_defined(&gg->async))
        async_abort(&gg->async);

    pool_unref(gg->pool);
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
    struct google_gadget *gw = istream_to_google_gadget(istream);

    assert(gw->parser != NULL);
    assert(gw->from_parser.sending_content);

    parser_read(gw->parser);
}

static void
istream_google_html_close(istream_t istream)
{
    struct google_gadget *gw = istream_to_google_gadget(istream);

    assert(gw->parser != NULL);
    assert(gw->from_parser.sending_content);

    parser_close(gw->parser);
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

    gg->waiting_for_locale = 0;

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
google_content_tag_finished(struct google_gadget *gw,
                            const struct parser_tag *tag)
{
    istream_t istream;

    switch (gw->from_parser.type) {
    case TYPE_NONE:
        break;

    case TYPE_HTML:
    case TYPE_HTML_INLINE:
        if (tag->type == TAG_OPEN) {
            if (gw->widget->from_request.proxy ||
                gw->from_parser.type == TYPE_HTML_INLINE) {
                gw->from_parser.sending_content = 1;
                gw->output = istream_google_html;
                gw->output.pool = gw->pool;

                gg_set_content(gw, istream_struct_cast(&gw->output), 1);
            } else {
                gw->widget->display = WIDGET_DISPLAY_IFRAME;

                istream = embed_iframe_widget(gw->pool, gw->env, gw->widget);
                gg_set_content(gw, istream, 0);

                parser_close(gw->parser);
            }
        } else {
            /* it's TAG_SHORT, handle that gracefully */

            gg_set_content(gw, NULL, 0);
        }

        return;

    case TYPE_URL:
        if (gw->from_parser.url == NULL)
            break;

        gw->widget->display = WIDGET_DISPLAY_EXTERNAL;
        gw->widget->class = gg_class(gw->pool, gw->from_parser.url);
        widget_determine_real_uri(gw->pool, gw->widget);

        istream = embed_iframe_widget(gw->pool, gw->env, gw->widget);
        gg_set_content(gw, istream, 0);

        parser_close(gw->parser);
        return;
    }

    google_send_error(gw, "malformed google gadget");
}


/*
 * parser callbacks
 *
 */

static void
google_parser_tag_start(const struct parser_tag *tag, void *ctx)
{
    struct google_gadget *gw = ctx;

    if (gw->from_parser.sending_content) {
        gw->from_parser.sending_content = 0;
        istream_invoke_eof(&gw->output);
    }

    if (!gw->has_locale && tag->type != TAG_CLOSE &&
        strref_cmp_literal(&tag->name, "locale") == 0) {
        gw->from_parser.tag = TAG_LOCALE;
        gw->has_locale = 1;
        gw->waiting_for_locale = 0;
    } else if (strref_cmp_literal(&tag->name, "content") == 0) {
        gw->from_parser.tag = TAG_CONTENT;
    } else {
        gw->from_parser.tag = TAG_NONE;
    }
}

static void
google_parser_tag_finished(const struct parser_tag *tag, void *ctx)
{
    struct google_gadget *gw = ctx;

    gw->from_parser.in_parser = 1;

    if (tag->type != TAG_CLOSE &&
        gw->from_parser.tag == TAG_CONTENT &&
        gw->delayed != NULL) {
        gw->from_parser.tag = TAG_NONE;
        google_content_tag_finished(gw, tag);
    } else {
        gw->from_parser.tag = TAG_NONE;
    }

    gw->from_parser.in_parser = 0;
}

static void
google_parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    struct google_gadget *gw = ctx;

    gw->from_parser.in_parser = 1;

    switch (gw->from_parser.tag) {
    case TAG_NONE:
        break;

    case TAG_LOCALE:
        if (strref_cmp_literal(&attr->name, "messages") == 0 &&
            !strref_is_empty(&attr->value) &&
            gw->delayed != NULL) {
            const char *url;

            gw->waiting_for_locale = 1;

            url = widget_absolute_uri(gw->pool, gw->widget,
                                      attr->value.data, attr->value.length);
            if (url == NULL)
                url = strref_dup(gw->pool, &attr->value);
            google_gadget_msg_load(gw, url);
        }
        break;

    case TAG_CONTENT:
        if (strref_cmp_literal(&attr->name, "type") == 0) {
            if (strref_cmp_literal(&attr->value, "url") == 0) {
                gw->from_parser.type = TYPE_URL;
                gw->from_parser.url = NULL;
            } else if (strref_cmp_literal(&attr->value, "html") == 0)
                gw->from_parser.type = TYPE_HTML;
            else if (strref_cmp_literal(&attr->value, "html-inline") == 0)
                gw->from_parser.type = TYPE_HTML_INLINE;
            else {
                google_send_error(gw, "unknown type attribute");
                gw->from_parser.in_parser = 0;
                return;
            }
        } else if (gw->from_parser.type == TYPE_URL &&
                   strref_cmp_literal(&attr->name, "href") == 0) {
            gw->from_parser.url = strref_dup(gw->pool, &attr->value);
        }

        break;
    }

    gw->from_parser.in_parser = 0;
}

static size_t
google_parser_cdata(const char *p, size_t length, int escaped, void *ctx)
{
    struct google_gadget *gw = ctx;

    if (!escaped && gw->from_parser.sending_content) {
        if (gw->has_locale && gw->waiting_for_locale)
            return 0;
        return istream_invoke_data(&gw->output, p, length);
    } else
        return length;
}

static void
google_parser_eof(void *ctx, off_t attr_unused length)
{
    struct google_gadget *gw = ctx;

    gw->parser = NULL;

    if (gw->has_locale && gw->waiting_for_locale)
        google_gadget_msg_close(gw);

    if (gw->from_parser.sending_content) {
        gw->from_parser.sending_content = 0;
        istream_invoke_eof(&gw->output);
    } else if (gw->delayed != NULL && !async_ref_defined(&gw->async))
        google_send_error(gw, "google gadget did not contain a valid Content element");

    pool_unref(gw->pool);
}

static void
google_parser_abort(void *ctx)
{
    struct google_gadget *gw = ctx;

    gw->parser = NULL;

    if (gw->has_locale && gw->waiting_for_locale)
        google_gadget_msg_close(gw);

    if (gw->from_parser.sending_content) {
        gw->from_parser.sending_content = 0;
        istream_invoke_abort(&gw->output);
    } else if (gw->delayed != NULL)
        google_send_error(gw, "google gadget retrieval aborted");

    pool_unref(gw->pool);
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
google_gadget_http_response(http_status_t status, strmap_t headers,
                            istream_t body, void *ctx)
{
    struct google_gadget *gw = ctx;
    const char *p;

    assert(gw->delayed != NULL);

    async_ref_clear(&gw->async);

    if (!http_status_is_success(status)) {
        if (body != NULL)
            istream_close(body);

        google_send_error(gw, "widget server reported error");
        return;
    }

    p = strmap_get(headers, "content-type");
    if (p == NULL || body == NULL ||
        (strncmp(p, "text/xml", 8) != 0 &&
         strncmp(p, "application/xml", 15) != 0)) {
        if (body != NULL)
            istream_close(body);

        google_send_error(gw, "text/xml expected");
        return;
    }

    gw->from_parser.tag = TAG_NONE;
    gw->from_parser.type = TYPE_NONE;
    gw->from_parser.sending_content = 0;
    gw->from_parser.in_parser = 0;
    gw->parser = parser_new(gw->pool, body,
                            &google_parser_handler, gw);
    istream_read(body);
}

static void
google_gadget_http_abort(void *ctx)
{
    struct google_gadget *gw = ctx;

    async_ref_clear(&gw->async);

    if (gw->delayed != NULL)
        istream_free(&gw->delayed);

    pool_unref(gw->pool);
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
    struct google_gadget *gw = async_to_gg(ao);

    assert((gw->delayed == NULL) == (gw->subst == NULL));

    if (gw->delayed == NULL)
        return;

    gw->delayed = NULL;
    istream_free(&gw->subst);

    if (gw->parser != NULL)
        parser_close(gw->parser);
    else if (async_ref_defined(&gw->async))
        async_abort(&gw->async);
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
    struct google_gadget *gw;
    const char *path;

    assert(widget != NULL);
    assert(widget->class != NULL);

    if (widget->from_request.proxy && strmap_get(env->args, "save") != NULL) {
        struct http_response_handler_ref handler_ref;
        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_NO_CONTENT,
                                              NULL, NULL);
        return;
    }

    pool_ref(pool);

    gw = p_malloc(pool, sizeof(*gw));
    gw->pool = pool;
    gw->env = env;
    gw->widget = widget;

    async_init(&gw->async_operation, &gg_async_operation);
    async_ref_set(async_ref, &gw->async_operation);

    gw->delayed = istream_delayed_new(pool, NULL);

    gw->subst = istream_subst_new(pool, gw->delayed);
    gw->parser = NULL;
    gw->has_locale = 0;

    path = widget_path(pool, widget);
    if (path != NULL)
        istream_subst_add(gw->subst,
                          "new _IG_Prefs()",
                          p_strcat(pool, "new _IG_Prefs(\"", path, "\")",
                                   NULL));

    http_response_handler_set(&gw->response_handler, handler, handler_ctx);

    url_stream_new(pool, env->http_client_stock,
                   HTTP_METHOD_GET, widget->class->uri,
                   NULL, NULL,
                   &google_gadget_handler, gw,
                   &gw->async);
}
