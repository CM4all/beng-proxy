/*
 * The translation request struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_REQUEST_HXX
#define BENG_PROXY_TRANSLATE_REQUEST_HXX

#include "Features.hxx"
#if TRANSLATION_ENABLE_HTTP
#include "net/SocketAddress.hxx"
#endif
#include "util/ConstBuffer.hxx"

#include <http/status.h>

#include <stddef.h>
#include <stdint.h>

struct TranslateRequest {
    const char *listener_tag;

#if TRANSLATION_ENABLE_HTTP
    SocketAddress local_address;
#endif

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

#if TRANSLATION_ENABLE_SESSION
    ConstBuffer<void> session;
#endif

    const char *param;

    /**
     * The payload of the #TRANSLATE_INTERNAL_REDIRECT packet.  If
     * ConstBuffer::IsNull(), then no #TRANSLATE_INTERNAL_REDIRECT
     * packet was received.
     */
    ConstBuffer<void> internal_redirect;

#if TRANSLATION_ENABLE_SESSION
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
#endif

#if TRANSLATION_ENABLE_HTTP
    /**
     * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
     * ConstBuffer::IsNull(), then no #TRANSLATE_WANT_FULL_URI packet
     * was received.
     */
    ConstBuffer<void> want_full_uri;
#endif

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

    /**
     * File contents.
     */
    ConstBuffer<void> read_file;

    const char *user;

    void Clear() {
        listener_tag = nullptr;
#if TRANSLATION_ENABLE_HTTP
        local_address = nullptr;
#endif
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
#if TRANSLATION_ENABLE_SESSION
        session = nullptr;
#endif
        param = nullptr;
        internal_redirect = nullptr;
#if TRANSLATION_ENABLE_SESSION
        check = nullptr;
        auth = nullptr;
#endif
#if TRANSLATION_ENABLE_HTTP
        want_full_uri = nullptr;
#endif
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
        read_file = nullptr;
        user = nullptr;
    }

    /**
     * Returns a name for this object to identify it in diagnostic
     * messages.
     */
    const char *GetDiagnosticName() const {
        if (uri != nullptr)
            return uri;

        if (widget_type != nullptr)
            return widget_type;

        if (suffix != nullptr)
            return suffix;

        return nullptr;
    }
};

#endif
