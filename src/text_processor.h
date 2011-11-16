/*
 * Process entities in a text stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TEXT_PROCESSOR_H
#define BENG_PROXY_TEXT_PROCESSOR_H

struct pool;
struct widget;
struct processor_env;

struct strmap *
text_processor_header_forward(struct pool *pool, struct strmap *headers);

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
struct istream *
text_processor(struct pool *pool, struct istream *istream,
               struct widget *widget, const struct processor_env *env);

#endif
