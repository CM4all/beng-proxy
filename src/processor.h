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
#include "http-response.h"

/** options for processor_new() */
enum processor_options {
    /** don't output anything from the input, don't generate <DIV>
        etc. */
    PROCESSOR_QUIET = 0x1,

    /** only output the HTML body */
    PROCESSOR_BODY = 0x2,

    /** generate JavaScript code */
    PROCESSOR_JSCRIPT = 0x4,

    /** generate JavaScript code for the root widget */
    PROCESSOR_JSCRIPT_ROOT = 0x8,

    /** enable the c:embed element */
    PROCESSOR_CONTAINER = 0x10,
};

struct hstock;
struct parsed_uri;
struct widget;
struct processor_env;

typedef istream_t (*processor_widget_callback_t)(pool_t pool,
                                                 struct processor_env *env,
                                                 struct widget *widget);

struct processor_env {
    pool_t pool;

    struct hstock *http_client_stock;

    const char *remote_host;

    const char *absolute_uri;

    /** the URI which was requested by the beng-proxy client */
    const struct parsed_uri *external_uri;

    /** semicolon-arguments in the external URI */
    strmap_t args;

    strmap_t request_headers;

    istream_t request_body;

    struct http_response_handler_ref response_handler;

    struct session *session;

    processor_widget_callback_t widget_callback;
};

void
processor_env_init(pool_t pool,
                   struct processor_env *env,
                   struct hstock *http_client_stock,
                   const char *remote_host,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   strmap_t args,
                   struct session *session,
                   strmap_t request_headers,
                   istream_t request_body,
                   processor_widget_callback_t widget_callback);

static inline struct processor_env * attr_malloc
processor_env_dup(pool_t pool, const struct processor_env *env)
{
    return (struct processor_env *)p_memdup(pool, env, sizeof(*env));
}

istream_t attr_malloc
processor_new(pool_t pool, istream_t istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options);

#endif
