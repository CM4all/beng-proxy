/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROCESSOR_H
#define __BENG_PROCESSOR_H

#include "istream.h"
#include "http.h"
#include "http-response.h"
#include "session.h"

/** options for processor_new() */
enum processor_options {
    /** rewrite URLs */
    PROCESSOR_REWRITE_URL = 0x1,

    /** enable the c:embed element */
    PROCESSOR_CONTAINER = 0x10,
};

struct parsed_uri;
struct widget;
struct async_operation_ref;

struct processor_env {
    pool_t pool;

    const char *partition_domain;

    const char *remote_host;

    const char *absolute_uri;

    /** the URI which was requested by the beng-proxy client */
    const struct parsed_uri *external_uri;

    /** semicolon-arguments in the external URI */
    struct strmap *args;

    /**
     * The HTTP method of the original request.
     */
    http_method_t method;

    struct strmap *request_headers;

    istream_t request_body;

    session_id_t session_id;
};

void
processor_env_init(pool_t pool,
                   struct processor_env *env,
                   const char *partition_domain,
                   const char *remote_host,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   struct strmap *args,
                   session_id_t session_id,
                   http_method_t method,
                   struct strmap *request_headers,
                   istream_t request_body);

void
processor_new(pool_t pool, http_status_t status,
              struct strmap *headers, istream_t istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options,
              const struct http_response_handler *handler,
              void *handler_ctx,
              struct async_operation_ref *async_ref);

#endif
