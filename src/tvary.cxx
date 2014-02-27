/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tvary.hxx"
#include "translate_response.hxx"
#include "strmap.h"
#include "growing-buffer.h"
#include "header-writer.h"

#include <beng-proxy/translation.h>

#include <assert.h>

static const char *
translation_vary_name(beng_translation_command cmd)
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
        return nullptr;
    }
}

static const char *
translation_vary_header(const TranslateResponse *response)
{
    assert(response != nullptr);

    static char buffer[256];
    char *p = buffer;
    for (unsigned i = 0, n = response->num_vary; i != n; ++i) {
        const auto cmd = beng_translation_command(response->vary[i]);
        const char *name = translation_vary_name(cmd);
        if (name == nullptr)
            continue;

        if (p > buffer)
            *p++ = ',';

        size_t length = strlen(name);
        memcpy(p, name, length);
        p += length;
    }

    return p > buffer ? buffer : nullptr;
}

struct strmap *
add_translation_vary_header(struct pool *pool, struct strmap *headers,
                            const TranslateResponse *response)
{
    assert(pool != nullptr);
    assert(response != nullptr);

    const char *value = translation_vary_header(response);
    if (value == nullptr)
        return headers;

    const char *old;
    if (headers == nullptr) {
        old = nullptr;
        headers = strmap_new(pool, 5);
    } else {
        old = strmap_get(headers, "vary");
    }

    if (old != nullptr)
        value = p_strcat(pool, old, ",", value, nullptr);

    strmap_set(headers, "vary", value);
    return headers;
}

void
write_translation_vary_header(struct growing_buffer *headers,
                              const TranslateResponse *response)
{
    assert(headers != nullptr);
    assert(response != nullptr);

    bool active = false;
    for (unsigned i = 0, n = response->num_vary; i != n; ++i) {
        const auto cmd = beng_translation_command(response->vary[i]);
        const char *name = translation_vary_name(cmd);
        if (name == nullptr)
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
