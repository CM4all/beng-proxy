/*
 * The translation request struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_REQUEST_HXX
#define BENG_PROXY_TRANSLATE_REQUEST_HXX

#include "util/ConstBuffer.hxx"

#include <http/status.h>

#include <stddef.h>
#include <stdint.h>

struct TranslateRequest {
    const struct sockaddr *local_address;
    size_t local_address_length;

    const char *remote_host;
    const char *host;
    const char *user_agent;
    const char *ua_class;
    const char *accept_language;

    /**
     * The value of the "Authorization" HTTP request header.
     */
    const char *authorization;

    const char *uri;
    const char *args;
    const char *query_string;
    const char *widget_type;
    ConstBuffer<void> session;
    const char *param;

    /**
     * The payload of the CHECK packet.  If ConstBuffer::IsNull(),
     * then no CHECK packet will be sent.
     */
    ConstBuffer<void> check;

    /**
     * The payload of the AUTH packet.  If ConstBuffer::IsNull(),
     * then no AUTH packet will be sent.
     */
    ConstBuffer<void> auth;

    /**
     * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
     * ConstBuffer::IsNull(), then no #TRANSLATE_WANT_FULL_URI packet
     * was received.
     */
    ConstBuffer<void> want_full_uri;

    ConstBuffer<uint16_t> want;

    ConstBuffer<void> file_not_found;

    ConstBuffer<void> content_type_lookup;

    const char *suffix;

    ConstBuffer<void> enotdir;

    ConstBuffer<void> directory_index;

    ConstBuffer<void> error_document;

    http_status_t error_document_status;

    ConstBuffer<void> probe_path_suffixes;
    const char *probe_suffix;

    void Clear() {
        local_address = nullptr;
        local_address_length = 0;
        remote_host = nullptr;
        host = nullptr;
        user_agent = nullptr;
        ua_class = nullptr;
        accept_language = nullptr;
        authorization = nullptr;
        uri = nullptr;
        args = nullptr;
        query_string = nullptr;
        widget_type = nullptr;
        session = nullptr;
        param = nullptr;
        check = nullptr;
        auth = nullptr;
        want_full_uri = nullptr;
        want = nullptr;
        file_not_found = nullptr;
        content_type_lookup = nullptr;
        suffix = nullptr;
        enotdir = nullptr;
        directory_index = nullptr;
        error_document = nullptr;
        error_document_status = http_status_t(0);
        probe_path_suffixes = nullptr;
        probe_suffix = nullptr;
    }
};

#endif
