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
    /** rewrite URLs */
    PROCESSOR_REWRITE_URL = 0x1,

    /** only output the HTML body */
    PROCESSOR_BODY = 0x2,

    /** generate JavaScript code */
    PROCESSOR_JSCRIPT = 0x4,

    /** generate JavaScript code for the root widget */
    PROCESSOR_JSCRIPT_ROOT = 0x8,

    /** enable the c:embed element */
    PROCESSOR_CONTAINER = 0x10,

    /** apply js_filter on JavaScript code */
    PROCESSOR_JS_FILTER = 0x20,

    /** generate JavaScript code which provides preferences data */
    PROCESSOR_JSCRIPT_PREFS = 0x40,
};

struct parsed_uri;
struct widget;
struct processor_env;
struct async_operation_ref;

typedef void (*processor_widget_callback_t)(pool_t pool,
                                            struct processor_env *env,
                                            struct widget *widget,
                                            const struct http_response_handler *handler,
                                            void *handler_ctx,
                                            struct async_operation_ref *async_ref);

struct processor_env {
    pool_t pool;

    struct http_cache *http_cache;

    const char *remote_host;

    const char *absolute_uri;

    /** the URI which was requested by the beng-proxy client */
    const struct parsed_uri *external_uri;

    /** semicolon-arguments in the external URI */
    strmap_t args;

    strmap_t request_headers;

    istream_t request_body;

    struct session *session;

    processor_widget_callback_t widget_callback;
};

void
processor_env_init(pool_t pool,
                   struct processor_env *env,
                   struct http_cache *http_cache,
                   const char *remote_host,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   strmap_t args,
                   struct session *session,
                   strmap_t request_headers,
                   istream_t request_body,
                   processor_widget_callback_t widget_callback);

static inline struct processor_env * __attr_malloc
processor_env_dup(pool_t pool, const struct processor_env *env)
{
    return (struct processor_env *)p_memdup(pool, env, sizeof(*env));
}

void
processor_new(pool_t pool, istream_t istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options,
              const struct http_response_handler *handler,
              void *handler_ctx,
              struct async_operation_ref *async_ref);

#endif
