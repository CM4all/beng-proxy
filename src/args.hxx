/*
 * Parse the argument list in an URI after the semicolon.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ARGS_HXX
#define BENG_PROXY_ARGS_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct strmap;

gcc_pure
struct strmap *
args_parse(struct pool *pool, const char *p, size_t length);

/**
 * Format the arguments into a string in the form
 * "KEY=VALUE&KEY2=VALUE2&...".
 *
 * @param replace_key add, replace or remove an entry in the args map
 * @param replace_value the new value or nullptr if the key should be removed
 */
gcc_pure
const char *
args_format_n(struct pool *pool, struct strmap *args,
              const char *replace_key, const char *replace_value,
              size_t replace_value_length,
              const char *replace_key2, const char *replace_value2,
              size_t replace_value2_length,
              const char *replace_key3, const char *replace_value3,
              size_t replace_value3_length,
              const char *remove_key);

gcc_pure
const char *
args_format(struct pool *pool, struct strmap *args,
            const char *replace_key, const char *replace_value,
            const char *replace_key2, const char *replace_value2,
            const char *remove_key);

#endif
