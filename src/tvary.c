/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tvary.h"
#include "translate-response.h"
#include "strmap.h"
#include "growing-buffer.h"
#include "header-writer.h"

#include <beng-proxy/translation.h>

#include <assert.h>

static const char *
translation_vary_name(enum beng_translation_command cmd)
{
    switch (cmd) {
    case TRANSLATE_SESSION:
        /* XXX need both "cookie2" and "cookie"? */
        return "cookie2";

    case TRANSLATE_LANGUAGE:
        return "accept-language";

    case TRANSLATE_AUTHORIZATION:
        return "authorization";

    case TRANSLATE_USER_AGENT:
    case TRANSLATE_UA_CLASS:
        return "user-agent";

    default:
        return NULL;
    }
}

static const char *
translation_vary_header(const struct translate_response *response)
{
    assert(response != NULL);

    static char buffer[256];
    char *p = buffer;
    for (unsigned i = 0, n = response->num_vary; i != n; ++i) {
        const char *name = translation_vary_name(response->vary[i]);
        if (name == NULL)
            continue;

        if (p > buffer)
            *p++ = ',';

        size_t length = strlen(name);
        memcpy(p, name, length);
        p += length;
    }

    return p > buffer ? buffer : NULL;
}

struct strmap *
add_translation_vary_header(struct pool *pool, struct strmap *headers,
                            const struct translate_response *response)
{
    assert(pool != NULL);
    assert(response != NULL);

    const char *value = translation_vary_header(response);
    if (value == NULL)
        return headers;

    const char *old;
    if (headers == NULL) {
        old = NULL;
        headers = strmap_new(pool, 5);
    } else {
        old = strmap_get(headers, "vary");
    }

    if (old != NULL)
        value = p_strcat(pool, old, ",", value, NULL);

    strmap_set(headers, "vary", value);
    return headers;
}

void
write_translation_vary_header(struct growing_buffer *headers,
                              const struct translate_response *response)
{
    assert(headers != NULL);
    assert(response != NULL);

    bool active = false;
    for (unsigned i = 0, n = response->num_vary; i != n; ++i) {
        const char *name = translation_vary_name(response->vary[i]);
        if (name == NULL)
            continue;

        if (active) {
            growing_buffer_write_buffer(headers, ",", 1);
        } else {
            active = true;
            header_write_begin(headers, "vary");
        }

        growing_buffer_write_string(headers, name);
    }

    if (active)
        header_write_finish(headers);
}
