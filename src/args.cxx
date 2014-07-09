/*
 * Parse the argument list in an URI after the semicolon.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "args.hxx"
#include "uri_escape.hxx"
#include "strmap.hxx"
#include "pool.h"

#include <string.h>

#ifdef NDEBUG
static const char ARGS_ESCAPE_CHAR = '$';
#else
/* hack: can be modified in cmdline.c; to be removed after all widgets
   have been fixed */
char ARGS_ESCAPE_CHAR = '$';
#endif

struct strmap *
args_parse(struct pool *pool, const char *p, size_t length)
{
    const char *end = p + length;
    struct strmap *args = strmap_new(pool, 16);

    do {
        const char *ampersand = (const char *)memchr(p, '&', end - p);
        const char *next = ampersand;
        if (ampersand == nullptr)
            ampersand = end;
        else
            ++next;

        const char *equals = (const char *)memchr(p, '=', ampersand - p);
        if (equals > p) {
            size_t value_length = ampersand - equals - 1;
            char *value = uri_unescape_dup(pool, equals + 1, value_length,
                                           ARGS_ESCAPE_CHAR);

            strmap_add(args, p_strndup(pool, p, equals - p), value);
        }

        p = next;
    } while (p != nullptr);

    return args;
}

const char *
args_format_n(struct pool *pool, struct strmap *args,
              const char *replace_key, const char *replace_value,
              size_t replace_value_length,
              const char *replace_key2, const char *replace_value2,
              size_t replace_value2_length,
              const char *replace_key3, const char *replace_value3,
              size_t replace_value3_length,
              const char *remove_key)
{
    const struct strmap_pair *pair;
    size_t length = 0;

    /* determine length */

    if (args != nullptr) {
        strmap_rewind(args);

        while ((pair = strmap_next(args)) != nullptr)
            length += strlen(pair->key) + 1 + strlen(pair->value) * 3 + 1;
    }

    if (replace_key != nullptr)
        length += strlen(replace_key) + 1 + replace_value_length * 3 + 1;

    if (replace_key2 != nullptr)
        length += strlen(replace_key2) + 1 + replace_value2_length * 3 + 1;

    if (replace_key3 != nullptr)
        length += strlen(replace_key3) + 1 + replace_value3_length * 3 + 1;

    /* allocate memory, format it */

    char *p = (char *)p_malloc(pool, length + 1);
    const char *const ret = p;

    if (args != nullptr) {
        strmap_rewind(args);

        while ((pair = strmap_next(args)) != nullptr) {
            if ((replace_key != nullptr && strcmp(pair->key, replace_key) == 0) ||
                (replace_key2 != nullptr && strcmp(pair->key, replace_key2) == 0) ||
                (replace_key3 != nullptr && strcmp(pair->key, replace_key3) == 0) ||
                (remove_key != nullptr && strcmp(pair->key, remove_key) == 0))
                continue;
            if (p > ret)
                *p++ = '&';
            length = strlen(pair->key);
            memcpy(p, pair->key, length);
            p += length;
            *p++ = '=';
            p += uri_escape(p, pair->value, strlen(pair->value),
                            ARGS_ESCAPE_CHAR);
        }
    }

    if (replace_key != nullptr) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key);
        memcpy(p, replace_key, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value, replace_value_length,
                        ARGS_ESCAPE_CHAR);
    }

    if (replace_key2 != nullptr) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key2);
        memcpy(p, replace_key2, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value2, replace_value2_length,
                        ARGS_ESCAPE_CHAR);
    }

    if (replace_key3 != nullptr) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key3);
        memcpy(p, replace_key3, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value3, replace_value3_length,
                        ARGS_ESCAPE_CHAR);
    }

    *p = 0;
    return ret;
}

const char *
args_format(struct pool *pool, struct strmap *args,
            const char *replace_key, const char *replace_value,
            const char *replace_key2, const char *replace_value2,
            const char *remove_key)
{
    return args_format_n(pool, args,
                         replace_key, replace_value,
                         replace_value == nullptr ? 0 : strlen(replace_value),
                         replace_key2, replace_value2,
                         replace_value2 == nullptr ? 0 : strlen(replace_value2),
                         nullptr, nullptr, 0,
                         remove_key);
}
