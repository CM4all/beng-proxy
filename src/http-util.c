/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-util.h"

#include <string.h>

int
http_list_contains(const char *list, const char *item)
{
    const char *comma;

    while (*list != 0) {
        comma = strchr(list, ',');
        if (comma == NULL)
            return strcmp(list, item) == 0;

        if (memcmp(list, item, comma - list) == 0)
            return 1;

        list = comma + 1;
    }

    return 0;
}
