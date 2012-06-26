/*
 * Process URLs in a CSS stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_PROCESSOR_H
#define BENG_PROXY_CSS_PROCESSOR_H

/** options for css_processor() */
enum css_processor_options {
    /** rewrite URLs */
    CSS_PROCESSOR_REWRITE_URL = 0x1,

    /** add prefix to marked CSS class names */
    CSS_PROCESSOR_PREFIX_CLASS = 0x2,

    /** add prefix to marked XML ids */
    CSS_PROCESSOR_PREFIX_ID = 0x4,
};

struct pool;
struct widget;
struct processor_env;

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
struct istream *
css_processor(struct pool *pool, struct istream *stream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options);

#endif
