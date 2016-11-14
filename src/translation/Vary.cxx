/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Vary.hxx"
#include "Response.hxx"
#include "strmap.hxx"
#include "GrowingBuffer.hxx"
#include "header_writer.hxx"
#include "pool.hxx"

#include <beng-proxy/translation.h>

#include <assert.h>
#include <string.h>

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
translation_vary_header(const TranslateResponse &response)
{
    static char buffer[256];
    char *p = buffer;

    for (auto i : response.vary) {
        const auto cmd = beng_translation_command(i);
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

void
add_translation_vary_header(StringMap &headers,
                            const TranslateResponse &response)
{
    const char *value = translation_vary_header(response);
    if (value == nullptr)
        return;

    const char *old = headers.Get("vary");
    if (old != nullptr)
        value = p_strcat(&headers.GetPool(), old, ",", value, nullptr);

    headers.Set("vary", value);
}

void
write_translation_vary_header(GrowingBuffer &headers,
                              const TranslateResponse &response)
{
    bool active = false;
    for (auto i : response.vary) {
        const auto cmd = beng_translation_command(i);
        const char *name = translation_vary_name(cmd);
        if (name == nullptr)
            continue;

        if (active) {
            headers.Write(",", 1);
        } else {
            active = true;
            header_write_begin(headers, "vary");
        }

        headers.Write(name);
    }

    if (active)
        header_write_finish(headers);
}
