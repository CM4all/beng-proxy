/*
 * Process entities in a text stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TEXT_PROCESSOR_HXX
#define BENG_PROXY_TEXT_PROCESSOR_HXX

#include <inline/compiler.h>

struct pool;
class Istream;
struct strmap;
struct Widget;
struct processor_env;

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
Istream *
text_processor(struct pool &pool, Istream &istream,
               const Widget &widget, const struct processor_env &env);

#endif
