/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AllocatedRequest.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <assert.h>

void
AllocatedTrafoRequest::Parse(beng_translation_command cmd,
                             const void *payload, size_t length)
{
    switch (cmd) {
    case TRANSLATE_BEGIN:
        Clear();

        if (length >= 1)
            protocol_version = *(uint8_t *)payload;

        break;

    case TRANSLATE_END:
        assert(false);
        gcc_unreachable();

    case TRANSLATE_URI:
        uri_buffer.assign((const char *)payload, length);
        uri = uri_buffer.c_str();
        break;

    case TRANSLATE_HOST:
        host_buffer.assign((const char *)payload, length);
        host = host_buffer.c_str();
        break;

    case TRANSLATE_ARGS:
        args_buffer.assign((const char *)payload, length);
        args = args_buffer.c_str();
        break;

    case TRANSLATE_QUERY_STRING:
        query_string_buffer.assign((const char *)payload, length);
        query_string = query_string_buffer.c_str();
        break;

    case TRANSLATE_USER_AGENT:
        user_agent_buffer.assign((const char *)payload, length);
        user_agent = user_agent_buffer.c_str();
        break;

    case TRANSLATE_UA_CLASS:
        ua_class_buffer.assign((const char *)payload, length);
        ua_class = ua_class_buffer.c_str();
        break;

    case TRANSLATE_LANGUAGE:
        accept_language_buffer.assign((const char *)payload, length);
        accept_language = accept_language_buffer.c_str();
        break;

    case TRANSLATE_AUTHORIZATION:
        authorization_buffer.assign((const char *)payload, length);
        authorization = authorization_buffer.c_str();
        break;

    default:
        daemon_log(4, "unknown translation packet: %u\n", unsigned(cmd));
        break;
    }
}
