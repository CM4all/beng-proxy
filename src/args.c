/*
 * Parse the argument list in an URI after the semicolon.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "args.h"

#include <string.h>

strmap_t
args_parse(pool_t pool, const char *p)
{
    strmap_t args = strmap_new(pool, 16);
    const char *and, *equals, *next;

    do {
        next = and = strchr(p, '&');
        if (and == NULL)
            and = p + strlen(p);
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
