/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROCESSOR_H
#define __BENG_PROCESSOR_H

#include "istream.h"
#include "strmap.h"
#include "http.h"

#include <sys/types.h>

/** options for processor_new() */
enum processor_options {
    /** don't output anything from the input, don't generate <DIV>
        etc. */
    PROCESSOR_QUIET = 0x1,

    /** only output the HTML body */
    PROCESSOR_BODY = 0x2,
};

struct widget;
struct processor_env;

typedef istream_t (*processor_widget_callback_t)(pool_t pool,
                                                 const struct processor_env *env,
                                                 struct widget *widget);

struct processor_env {
    /** the URI which was requested by the beng-proxy client */
    const struct parsed_uri *external_uri;

    /** semicolon-arguments in the external URI */
    strmap_t args;

    off_t request_content_length;

    istream_t request_body;

    /** which widget is displayed in this frame? */
    const char *frame;

    void (*proxy_callback)(http_status_t status,
                           strmap_t headers,
                           off_t content_length, istream_t body,
                           void *ctx);

    void *proxy_callback_ctx;

    /** which widget is focused, i.e. gets the request body and the
        query string? */
    const char *focus;

    struct session *session;

    char session_id_buffer[9];

    processor_widget_callback_t widget_callback;
};

void
processor_env_init(pool_t pool, struct processor_env *env,
                   const struct parsed_uri *uri,
                   off_t request_content_length,
                   istream_t request_body,
                   processor_widget_callback_t widget_callback);

istream_t attr_malloc
processor_new(pool_t pool, istream_t istream,
              struct widget *widget,
              const struct processor_env *env,
              unsigned options);

#endif
