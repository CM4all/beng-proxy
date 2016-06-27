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
struct StringMap;
struct StringView;

gcc_pure
StringMap *
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
args_format_n(struct pool *pool, const StringMap *args,
              const char *replace_key, StringView replace_value,
              const char *replace_key2, StringView replace_value2,
              const char *replace_key3, StringView replace_value3,
              const char *remove_key);

gcc_pure
const char *
args_format(struct pool *pool, const StringMap *args,
            const char *replace_key, const char *replace_value,
            const char *replace_key2, const char *replace_value2,
            const char *remove_key);

#endif
