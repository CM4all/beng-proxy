/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "InvalidateParser.hxx"
#include "Request.hxx"
#include "util/ByteOrder.hxx"
#include "pool.hxx"

static bool
apply_translation_packet(TranslateRequest &request,
                         enum TranslationCommand command,
                         const char *payload, size_t payload_length)
{
    switch (command) {
    case TranslationCommand::URI:
        request.uri = payload;
        break;

    case TranslationCommand::SESSION:
        request.session = { payload, payload_length };
        break;

        /* XXX
    case TranslationCommand::LOCAL_ADDRESS:
        request.local_address = payload;
        break;
        */

    case TranslationCommand::REMOTE_HOST:
        request.remote_host = payload;
        break;

    case TranslationCommand::HOST:
        request.host = payload;
        break;

    case TranslationCommand::LANGUAGE:
        request.accept_language = payload;
        break;

    case TranslationCommand::USER_AGENT:
        request.user_agent = payload;
        break;

    case TranslationCommand::UA_CLASS:
        request.ua_class = payload;
        break;

    case TranslationCommand::QUERY_STRING:
        request.query_string = payload;
        break;

    default:
        /* unsupported */
        return false;
    }

    return true;
}

unsigned
decode_translation_packets(struct pool &pool, TranslateRequest &request,
                           TranslationCommand *cmds, unsigned max_cmds,
                           const void *data, size_t length,
                           const char **site_r)
{
    *site_r = NULL;

    unsigned num_cmds = 0;

    if (length % 4 != 0)
        /* must be padded */
        return 0;

    while (length > 0) {
        const auto *header = (const TranslationHeader *)data;
        if (length < sizeof(*header))
            return 0;

        size_t payload_length = FromBE16(header->length);
        const auto command =
            TranslationCommand(FromBE16(uint16_t(header->command)));

        data = header + 1;
        length -= sizeof(*header);

        if (length < payload_length)
            return 0;

        char *payload = payload_length > 0
            ? p_strndup(&pool, (const char *)data, payload_length)
            : NULL;
        if (command == TranslationCommand::SITE)
            *site_r = payload;
        else if (apply_translation_packet(request, command, payload,
                                          payload_length)) {
            if (num_cmds >= max_cmds)
                return 0;

            cmds[num_cmds++] = command;
        } else
            return 0;

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = (const char *)data + payload_length;
        length -= payload_length;
    }

    return num_cmds;
}
