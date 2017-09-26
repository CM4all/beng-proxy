/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "AcmeClient.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Dummy.hxx"
#include "ssl/Key.hxx"
#include "util/Exception.hxx"

GlueHttpResponse
AcmeClient::FakeRequest(http_method_t method, const char *uri,
                        ConstBuffer<void> body)
try {
    (void)method;
    (void)body;

    if (strcmp(uri, "/acme/new-authz") == 0) {
        std::multimap<std::string, std::string> response_headers = {
            {"content-type", "application/json"},
        };

        return GlueHttpResponse(HTTP_STATUS_CREATED,
                                std::move(response_headers),
                                "{"
                                "  \"status\": \"pending\","
                                "  \"identifier\": {\"type\": \"dns\", \"value\": \"example.org\"},"
                                "  \"challenges\": ["
                                "    {"
                                "      \"type\": \"tls-sni-01\","
                                "      \"token\": \"example-token-tls-sni-01\","
                                "      \"uri\": \"http://xyz/example/tls-sni-01/uri\""
                                "    }"
                                "  ]"
                                "}");
    } else if (strcmp(uri, "/example/tls-sni-01/uri") == 0) {
        std::multimap<std::string, std::string> response_headers = {
            {"content-type", "application/json"},
        };

        return GlueHttpResponse(HTTP_STATUS_ACCEPTED,
                                std::move(response_headers),
                                "{"
                                "  \"status\": \"valid\""
                                "}");
    } else if (strcmp(uri, "/acme/new-cert") == 0) {
        auto key = GenerateRsaKey();
        auto cert = MakeSelfSignedDummyCert(*key, "example.com");

        const SslBuffer cert_buffer(*cert);

        auto response_body = ConstBuffer<char>::FromVoid(cert_buffer.get());
        return GlueHttpResponse(HTTP_STATUS_CREATED, {},
                                std::string(response_body.data,
                                            response_body.size));
    } else
        return GlueHttpResponse(HTTP_STATUS_NOT_FOUND, {},
                                "Not found");
} catch (...) {
    return GlueHttpResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR, {},
                            GetFullMessage(std::current_exception()));
}
