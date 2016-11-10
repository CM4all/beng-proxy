/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Marshal.hxx"
#include "Request.hxx"
#include "growing_buffer.hxx"
#include "util/RuntimeError.hxx"

#include <beng-proxy/translation.h>

#include <socket/address.h>

#include <string.h>

static void
write_packet_n(GrowingBuffer *gb, uint16_t command,
               const void *payload, size_t length)
{
    if (length >= 0xffff)
        throw FormatRuntimeError("payload for translate command %u too large",
                                 command);

    struct beng_translation_header header;
    header.length = (uint16_t)length;
    header.command = command;

    gb->Write(&header, sizeof(header));
    if (length > 0)
        gb->Write(payload, length);
}

static void
write_packet(GrowingBuffer *gb, uint16_t command, const char *payload)
{
    write_packet_n(gb, command, payload,
                   payload != nullptr ? strlen(payload) : 0);
}

template<typename T>
static void
write_buffer(GrowingBuffer *gb, uint16_t command,
             ConstBuffer<T> buffer)
{
    auto b = buffer.ToVoid();
    write_packet_n(gb, command, b.data, b.size);
}

/**
 * Forward the command to write_packet() only if #payload is not nullptr.
 */
static void
write_optional_packet(GrowingBuffer *gb, uint16_t command, const char *payload)
{
    if (payload != nullptr)
        write_packet(gb, command, payload);
}

template<typename T>
static void
write_optional_buffer(GrowingBuffer *gb, uint16_t command,
                      ConstBuffer<T> buffer)
{
    if (!buffer.IsNull())
        write_buffer(gb, command, buffer);
}

static void
write_short(GrowingBuffer *gb, uint16_t command, uint16_t payload)
{
    write_packet_n(gb, command, &payload, sizeof(payload));
}

static void
write_sockaddr(GrowingBuffer *gb,
               uint16_t command, uint16_t command_string,
               SocketAddress address)
{
    assert(!address.IsNull());

    char address_string[1024];
    write_packet_n(gb, command,
                   address.GetAddress(), address.GetSize());

    if (socket_address_to_string(address_string, sizeof(address_string),
                                 address.GetAddress(), address.GetSize()))
        write_packet(gb, command_string, address_string);
}

static void
write_optional_sockaddr(GrowingBuffer *gb,
                        uint16_t command, uint16_t command_string,
                        SocketAddress address)
{
    if (!address.IsNull())
        write_sockaddr(gb, command, command_string, address);
}

GrowingBuffer
MarshalTranslateRequest(struct pool &pool, uint8_t PROTOCOL_VERSION,
                        const TranslateRequest &request)
{
    GrowingBuffer gb(pool, 512);

    write_packet_n(&gb, TRANSLATE_BEGIN,
                   &PROTOCOL_VERSION, sizeof(PROTOCOL_VERSION));
    write_optional_buffer(&gb, TRANSLATE_ERROR_DOCUMENT,
                          request.error_document);

    if (request.error_document_status != 0)
        write_short(&gb, TRANSLATE_STATUS,
                    request.error_document_status);

    write_optional_packet(&gb, TRANSLATE_LISTENER_TAG,
                          request.listener_tag);
    write_optional_sockaddr(&gb, TRANSLATE_LOCAL_ADDRESS,
                            TRANSLATE_LOCAL_ADDRESS_STRING,
                            request.local_address);
    write_optional_packet(&gb, TRANSLATE_REMOTE_HOST,
                          request.remote_host);
    write_optional_packet(&gb, TRANSLATE_HOST, request.host);
    write_optional_packet(&gb, TRANSLATE_USER_AGENT, request.user_agent);
    write_optional_packet(&gb, TRANSLATE_UA_CLASS, request.ua_class);
    write_optional_packet(&gb, TRANSLATE_LANGUAGE, request.accept_language);
    write_optional_packet(&gb, TRANSLATE_AUTHORIZATION, request.authorization);
    write_optional_packet(&gb, TRANSLATE_URI, request.uri);
    write_optional_packet(&gb, TRANSLATE_ARGS, request.args);
    write_optional_packet(&gb, TRANSLATE_QUERY_STRING, request.query_string);
    write_optional_packet(&gb, TRANSLATE_WIDGET_TYPE, request.widget_type);
    write_optional_buffer(&gb, TRANSLATE_SESSION, request.session);
    write_optional_buffer(&gb, TRANSLATE_INTERNAL_REDIRECT,
                          request.internal_redirect);
    write_optional_buffer(&gb, TRANSLATE_CHECK, request.check);
    write_optional_buffer(&gb, TRANSLATE_AUTH, request.auth);
    write_optional_buffer(&gb, TRANSLATE_WANT_FULL_URI, request.want_full_uri);
    write_optional_buffer(&gb, TRANSLATE_WANT, request.want);
    write_optional_buffer(&gb, TRANSLATE_FILE_NOT_FOUND,
                          request.file_not_found);
    write_optional_buffer(&gb, TRANSLATE_CONTENT_TYPE_LOOKUP,
                          request.content_type_lookup);
    write_optional_packet(&gb, TRANSLATE_SUFFIX, request.suffix);
    write_optional_buffer(&gb, TRANSLATE_ENOTDIR, request.enotdir);
    write_optional_buffer(&gb, TRANSLATE_DIRECTORY_INDEX,
                          request.directory_index);
    write_optional_packet(&gb, TRANSLATE_PARAM, request.param);
    write_optional_buffer(&gb, TRANSLATE_PROBE_PATH_SUFFIXES,
                          request.probe_path_suffixes);
    write_optional_packet(&gb, TRANSLATE_PROBE_SUFFIX,
                          request.probe_suffix);
    write_optional_buffer(&gb, TRANSLATE_READ_FILE,
                          request.read_file);
    write_optional_packet(&gb, TRANSLATE_USER, request.user);
    write_packet(&gb, TRANSLATE_END, nullptr);

    return gb;
}
