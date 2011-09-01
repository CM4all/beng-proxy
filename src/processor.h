/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROCESSOR_H
#define __BENG_PROCESSOR_H

#include <http/method.h>
#include <http/status.h>

/** options for processor_new() */
enum processor_options {
    /** rewrite URLs */
    PROCESSOR_REWRITE_URL = 0x1,

    /** add prefix to marked CSS class names */
    PROCESSOR_PREFIX_CSS_CLASS = 0x2,

    /**
     * Default URI rewrite mode is base=widget mode=focus.
     */
    PROCESSOR_FOCUS_WIDGET = 0x4,

    /** enable the c:embed element */
    PROCESSOR_CONTAINER = 0x10,
};

struct pool;
struct parsed_uri;
struct widget;
struct widget_lookup_handler;
struct async_operation_ref;
struct processor_env;

struct strmap *
processor_header_forward(struct pool *pool, struct strmap *headers);

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
struct istream *
processor_process(struct pool *pool, struct istream *istream,
                  struct widget *widget,
                  struct processor_env *env,
                  unsigned options);

/**
 * Process the specified istream, and find the specified widget.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
processor_lookup_widget(struct pool *pool, http_status_t status,
                        struct istream *istream,
                        struct widget *widget, const char *id,
                        struct processor_env *env,
                        unsigned options,
                        const struct widget_lookup_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref);

#endif
