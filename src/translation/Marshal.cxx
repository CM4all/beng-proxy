/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Marshal.hxx"
#include "Request.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"

#include <socket/address.h>

#include <string.h>

void
TranslationMarshaller::Write(enum beng_translation_command command,
                             ConstBuffer<void> payload)
{
    if (payload.size >= 0xffff)
        throw FormatRuntimeError("payload for translate command %u too large",
                                 command);

    struct beng_translation_header header;
    header.length = (uint16_t)payload.size;
    header.command = (uint16_t)command;

    buffer.Write(&header, sizeof(header));

    if (!payload.IsEmpty())
        buffer.Write(payload.data, payload.size);
}

void
TranslationMarshaller::Write(enum beng_translation_command command,
                                   const char *payload)
{
    Write(command, StringView(payload));
}

void
TranslationMarshaller::Write(enum beng_translation_command command,
                             enum beng_translation_command command_string,
                             SocketAddress address)
{
    assert(!address.IsNull());

    Write(command, ConstBuffer<void>(address.GetAddress(), address.GetSize()));

    char address_string[1024];

    if (socket_address_to_string(address_string, sizeof(address_string),
                                 address.GetAddress(), address.GetSize()))
        Write(command_string, address_string);
}

void
TranslationMarshaller::WriteOptional(enum beng_translation_command command,
                                     enum beng_translation_command command_string,
                                     SocketAddress address)
{
    if (!address.IsNull())
        Write(command, command_string, address);
}

GrowingBuffer
MarshalTranslateRequest(struct pool &pool, uint8_t PROTOCOL_VERSION,
                        const TranslateRequest &request)
{
    TranslationMarshaller m(pool);

    m.WriteT(TRANSLATE_BEGIN, PROTOCOL_VERSION);
    m.WriteOptional(TRANSLATE_ERROR_DOCUMENT,
                          request.error_document);

    if (request.error_document_status != 0)
        m.Write16(TRANSLATE_STATUS, request.error_document_status);

    m.WriteOptional(TRANSLATE_LISTENER_TAG,
                          request.listener_tag);
    m.WriteOptional(TRANSLATE_LOCAL_ADDRESS,
                    TRANSLATE_LOCAL_ADDRESS_STRING,
                    request.local_address);
    m.WriteOptional(TRANSLATE_REMOTE_HOST,
                          request.remote_host);
    m.WriteOptional(TRANSLATE_HOST, request.host);
    m.WriteOptional(TRANSLATE_USER_AGENT, request.user_agent);
    m.WriteOptional(TRANSLATE_UA_CLASS, request.ua_class);
    m.WriteOptional(TRANSLATE_LANGUAGE, request.accept_language);
    m.WriteOptional(TRANSLATE_AUTHORIZATION, request.authorization);
    m.WriteOptional(TRANSLATE_URI, request.uri);
    m.WriteOptional(TRANSLATE_ARGS, request.args);
    m.WriteOptional(TRANSLATE_QUERY_STRING, request.query_string);
    m.WriteOptional(TRANSLATE_WIDGET_TYPE, request.widget_type);
    m.WriteOptional(TRANSLATE_SESSION, request.session);
    m.WriteOptional(TRANSLATE_INTERNAL_REDIRECT,
                          request.internal_redirect);
    m.WriteOptional(TRANSLATE_CHECK, request.check);
    m.WriteOptional(TRANSLATE_AUTH, request.auth);
    m.WriteOptional(TRANSLATE_WANT_FULL_URI, request.want_full_uri);
    m.WriteOptional(TRANSLATE_WANT, request.want);
    m.WriteOptional(TRANSLATE_FILE_NOT_FOUND,
                          request.file_not_found);
    m.WriteOptional(TRANSLATE_CONTENT_TYPE_LOOKUP,
                          request.content_type_lookup);
    m.WriteOptional(TRANSLATE_SUFFIX, request.suffix);
    m.WriteOptional(TRANSLATE_ENOTDIR, request.enotdir);
    m.WriteOptional(TRANSLATE_DIRECTORY_INDEX,
                          request.directory_index);
    m.WriteOptional(TRANSLATE_PARAM, request.param);
    m.WriteOptional(TRANSLATE_PROBE_PATH_SUFFIXES,
                          request.probe_path_suffixes);
    m.WriteOptional(TRANSLATE_PROBE_SUFFIX,
                          request.probe_suffix);
    m.WriteOptional(TRANSLATE_READ_FILE,
                          request.read_file);
    m.WriteOptional(TRANSLATE_USER, request.user);
    m.Write(TRANSLATE_END);

    return m.Commit();
}
