/*
 * Parse the argument list in an URI after the semicolon.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "args.h"
#include "uri-escape.h"
#include "strmap.h"

#include <string.h>

struct strmap *
args_parse(pool_t pool, const char *p, size_t length)
{
    const char *end = p + length;
    struct strmap *args = strmap_new(pool, 16);
    const char *and, *equals, *next;

    do {
        next = and = memchr(p, '&', end - p);
        if (and == NULL)
            and = end;
        else
            ++next;
        equals = memchr(p, '=', and - p);
        if (equals > p) {
            size_t value_length = and - equals - 1;
            char *value = p_strndup(pool, equals + 1, value_length);
            value_length = uri_unescape_inplace(value, value_length);
            value[value_length] = 0;

            strmap_addn(args,
                        p_strndup(pool, p, equals - p),
                        value);
        }

        p = next;
    } while (p != NULL);

    return args;
}

const char *
args_format_n(pool_t pool, struct strmap *args,
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
    char *ret, *p;

    /* determine length */

    if (args != NULL) {
        strmap_rewind(args);

        while ((pair = strmap_next(args)) != NULL)
            length += strlen(pair->key) + 1 + strlen(pair->value) * 3 + 1;
    }

    if (replace_key != NULL)
        length += strlen(replace_key) + 1 + replace_value_length * 3 + 1;

    if (replace_key2 != NULL)
        length += strlen(replace_key2) + 1 + replace_value2_length * 3 + 1;

    if (replace_key3 != NULL)
        length += strlen(replace_key3) + 1 + replace_value3_length * 3 + 1;

    /* allocate memory, format it */

    ret = p = p_malloc(pool, length);

    if (args != NULL) {
        strmap_rewind(args);

        while ((pair = strmap_next(args)) != NULL) {
            if ((replace_key != NULL && strcmp(pair->key, replace_key)) == 0 ||
                (replace_key2 != NULL && strcmp(pair->key, replace_key2) == 0) ||
                (replace_key3 != NULL && strcmp(pair->key, replace_key3) == 0) ||
                (remove_key != NULL && strcmp(pair->key, remove_key) == 0))
                continue;
            if (p > ret)
                *p++ = '&';
            length = strlen(pair->key);
            memcpy(p, pair->key, length);
            p += length;
            *p++ = '=';
            p += uri_escape(p, pair->value, strlen(pair->value));
        }
    }

    if (replace_key != NULL) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key);
        memcpy(p, replace_key, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value, replace_value_length);
    }

    if (replace_key2 != NULL) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key2);
        memcpy(p, replace_key2, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value2, replace_value2_length);
    }

    if (replace_key3 != NULL) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key3);
        memcpy(p, replace_key3, length);
        p += length;
        *p++ = '=';
        p += uri_escape(p, replace_value3, replace_value3_length);
    }

    *p = 0;
    return ret;
}

const char *
args_format(pool_t pool, struct strmap *args,
            const char *replace_key, const char *replace_value,
            const char *replace_key2, const char *replace_value2,
            const char *remove_key)
{
    return args_format_n(pool, args,
                         replace_key, replace_value,
                         replace_value == NULL ? 0 : strlen(replace_value),
                         replace_key2, replace_value2,
                         replace_value2 == NULL ? 0 : strlen(replace_value2),
                         NULL, NULL, 0,
                         remove_key);
}
