/*
 * Emulation layer for Google gadgets.
 *
 * Load and parse locale data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "google-gadget-internal.h"
#include "parser.h"
#include "url-stream.h"
#include "http-response.h"
#include "processor.h"

static void
gg_msg_finish(struct google_gadget *gg)
{
    if (!gg->msg.in_msg_tag)
        return;

    if (gg->msg.key != NULL)
        istream_subst_add(gg->subst, gg->msg.key, gg->msg.value);

    gg->msg.in_msg_tag = 0;
}

/*
 * parser callbacks
 *
 */

static void
gg_msg_parser_tag_start(const struct parser_tag *tag, void *ctx)
{
    struct google_gadget *gg = ctx;

    gg_msg_finish(gg);

    if (strref_cmp_literal(&tag->name, "msg") == 0 &&
        tag->type != TAG_CLOSE) {
        gg->msg.in_msg_tag = 1;
        gg->msg.key = NULL;
        gg->msg.value = NULL;
        /* XXX */
    }
}

static void
gg_msg_parser_tag_finished(const struct parser_tag *tag attr_unused,
                           void *ctx)
{
    struct google_gadget *gg = ctx;

    if (tag->type != TAG_OPEN)
        gg_msg_finish(gg);
}

static void
gg_msg_parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    struct google_gadget *gg = ctx;

    if (!gg->msg.in_msg_tag)
        return;

    if (strref_cmp_literal(&attr->name, "name") == 0 &&
        !strref_is_empty(&attr->value))
        gg->msg.key = p_strncat(gg->pool,
                                "__MSG_", (size_t)6,
                                attr->value.data, attr->value.length,
                                "__", (size_t)2,
                                NULL);
}

static size_t
gg_msg_parser_cdata(const char *p, size_t length, int escaped attr_unused, void *ctx)
{
    struct google_gadget *gg = ctx;

    if (gg->msg.in_msg_tag && gg->msg.key != NULL) {
        if (gg->msg.value == NULL)
            gg->msg.value = p_strndup(gg->pool, p, length);
        else
            gg->msg.value = p_strncat(gg->pool,
                                      gg->msg.value, strlen(gg->msg.value),
                                      p, length,
                                      NULL);
    }

    return length;
}

static void
gg_msg_parser_eof(void *ctx, off_t attr_unused length)
{
    struct google_gadget *gg = ctx;

    gg->msg.parser = NULL;

    gg_msg_finish(gg);

    google_gadget_msg_eof(gg);
}

static void
gg_msg_parser_abort(void *ctx)
{
    struct google_gadget *gg = ctx;

    gg->msg.parser = NULL;

    google_gadget_msg_abort(gg);
}

static const struct parser_handler gg_msg_parser_handler = {
    .tag_start = gg_msg_parser_tag_start,
    .tag_finished = gg_msg_parser_tag_finished,
    .attr_finished = gg_msg_parser_attr_finished,
    .cdata = gg_msg_parser_cdata,
    .eof = gg_msg_parser_eof,
    .abort = gg_msg_parser_abort,
};


/*
 * url_stream handler (gadget description)
 *
 */

static void
gg_msg_http_response(http_status_t status, strmap_t headers,
                     istream_t body, void *ctx)
{
    struct google_gadget *gg = ctx;
    const char *p;

    async_ref_clear(&gg->async);

    if (!http_status_is_success(status)) {
        if (body != NULL)
            istream_close(body);

        /* XXX google_send_error(gg, "widget server reported error"); */
        return;
    }

    p = strmap_get(headers, "content-type");
    if (p == NULL || body == NULL ||
        (strncmp(p, "text/xml", 8) != 0 &&
         strncmp(p, "application/xml", 15) != 0)) {
        if (body != NULL)
            istream_close(body);

        /* XXX google_send_error(gg, "text/xml expected"); */
        return;
    }

    gg->msg.in_msg_tag = 0;
    gg->msg.parser = parser_new(gg->pool, body,
                                &gg_msg_parser_handler, gg);
    istream_read(body);
}

static void
gg_msg_http_abort(void *ctx)
{
    struct google_gadget *gg = ctx;

    async_ref_clear(&gg->async);

    google_gadget_msg_abort(gg);
}

static const struct http_response_handler gg_msg_http_handler = {
    .response = gg_msg_http_response,
    .abort = gg_msg_http_abort,
};


/*
 * constructor
 *
 */

void
google_gadget_msg_load(struct google_gadget *gg, const char *url)
{
    url_stream_new(gg->pool, gg->env->http_client_stock,
                   HTTP_METHOD_GET, url,
                   NULL, NULL,
                   &gg_msg_http_handler, gg,
                   &gg->async);
}

void
google_gadget_msg_close(struct google_gadget *gg)
{
    if (gg->msg.parser != NULL)
        parser_close(gg->msg.parser);

    assert(gg->msg.parser == NULL);
}
