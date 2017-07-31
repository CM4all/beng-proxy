/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "InvalidateParser.hxx"
#include "Request.hxx"
#include "util/ByteOrder.hxx"
#include "util/RuntimeError.hxx"
#include "pool.hxx"

static void
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
        throw FormatRuntimeError("Unsupported packet: %u", unsigned(command));
    }
}

TranslationInvalidateRequest
ParseTranslationInvalidateRequest(struct pool &pool,
                                  const void *data, size_t length)
{
    TranslationInvalidateRequest request;
    request.Clear();

    if (length % 4 != 0)
        /* must be padded */
        throw std::runtime_error("Not padded");

    while (length > 0) {
        const auto *header = (const TranslationHeader *)data;
        if (length < sizeof(*header))
            throw std::runtime_error("Partial header");

        size_t payload_length = FromBE16(header->length);
        const auto command =
            TranslationCommand(FromBE16(uint16_t(header->command)));

        data = header + 1;
        length -= sizeof(*header);

        if (length < payload_length)
            throw std::runtime_error("Truncated payload");

        const char *payload = payload_length > 0
            ? p_strndup(&pool, (const char *)data, payload_length)
            : "";
        if (command == TranslationCommand::SITE)
            request.site = payload;
        else {
            apply_translation_packet(request, command, payload,
                                     payload_length);

            if (!request.commands.checked_append(command))
                throw std::runtime_error("Too many commands");
        }

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = (const char *)data + payload_length;
        length -= payload_length;
    }

    return request;
}
