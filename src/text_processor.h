/*
 * Process entities in a text stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TEXT_PROCESSOR_H
#define BENG_PROXY_TEXT_PROCESSOR_H

#include <inline/compiler.h>

#include <stdbool.h>

struct pool;
struct strmap;
struct widget;
struct processor_env;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if the resource described by the specified headers can be
 * processed by the text processor.
 */
gcc_pure
bool
text_processor_allowed(const struct strmap *headers);

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
struct istream *
text_processor(struct pool *pool, struct istream *istream,
               const struct widget *widget, const struct processor_env *env);

#ifdef __cplusplus
}
#endif

#endif
