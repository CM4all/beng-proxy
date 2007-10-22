/*
 * Parse the argument list in an URI after the semicolon.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "args.h"

#include <string.h>

strmap_t
args_parse(pool_t pool, const char *p, size_t length)
{
    const char *end = p + length;
    strmap_t args = strmap_new(pool, 16);
    const char *and, *equals, *next;

    do {
        next = and = memchr(p, '&', end - p);
        if (and == NULL)
            and = end;
        else
            ++next;
        equals = memchr(p, '=', and - p);
        if (equals > p)
            strmap_addn(args,
                        p_strndup(pool, p, equals - p),
                        p_strndup(pool, equals + 1, and - equals - 1));

        p = next;
    } while (p != NULL);

    return args;
}

const char *
args_format(pool_t pool, strmap_t args,
            const char *replace_key, const char *replace_value,
            const char *replace_key2, const char *replace_value2)
{
    const struct strmap_pair *pair;
    size_t length = 0;
    char *ret, *p;

    /* determine length */

    if (args != NULL) {
        strmap_rewind(args);

        while ((pair = strmap_next(args)) != NULL)
            length += strlen(pair->key) + 1 + strlen(pair->value) + 1;
    }

    if (replace_key != NULL && replace_value != NULL)
        length += strlen(replace_key) + 1 + strlen(replace_value) + 1;

    if (replace_key2 != NULL && replace_value2 != NULL)
        length += strlen(replace_key2) + 1 + strlen(replace_value2) + 1;

    /* allocate memory, format it */

    ret = p = p_malloc(pool, length);

    if (args != NULL) {
        strmap_rewind(args);

        while ((pair = strmap_next(args)) != NULL) {
            if ((replace_key != NULL && strcmp(pair->key, replace_key)) == 0 ||
                (replace_key2 != NULL && strcmp(pair->key, replace_key2) == 0))
                continue;
            if (p > ret)
                *p++ = '&';
            length = strlen(pair->key);
            memcpy(p, pair->key, length);
            p += length;
            *p++ = '=';
            length = strlen(pair->value);
            memcpy(p, pair->value, length);
            p += length;
        }
    }

    if (replace_key != NULL && replace_value != NULL) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key);
        memcpy(p, replace_key, length);
        p += length;
        *p++ = '=';
        length = strlen(replace_value);
        memcpy(p, replace_value, length);
        p += length;
    }

    if (replace_key2 != NULL && replace_value2 != NULL) {
        if (p > ret)
            *p++ = '&';
        length = strlen(replace_key2);
        memcpy(p, replace_key2, length);
        p += length;
        *p++ = '=';
        length = strlen(replace_value2);
        memcpy(p, replace_value2, length);
        p += length;
    }

    *p = 0;
    return ret;
}
