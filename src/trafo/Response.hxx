/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

#ifndef TRAFO_RESPONSE_HXX
#define TRAFO_RESPONSE_HXX

#include "translation/Protocol.hxx"
#include "http/Status.h"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <stdint.h>
#include <stddef.h>

class TrafoResponse {
    uint8_t *buffer;
    size_t capacity, size;

public:
    TrafoResponse()
        :buffer(nullptr), capacity(0), size(0) {
        Packet(TranslationCommand::BEGIN);
    }

    TrafoResponse(TrafoResponse &&other)
        :buffer(other.buffer), capacity(other.capacity), size(other.size) {
        other.buffer = nullptr;
    }

    ~TrafoResponse() {
        delete[] buffer;
    }

    /**
     * Append an empty packet.
     */
    void Packet(TranslationCommand cmd);

    void Packet(TranslationCommand cmd, ConstBuffer<void> payload);

    void Packet(TranslationCommand cmd,
                const void *payload, size_t length) {
        Packet(cmd, {payload, length});
    }

    void Packet(TranslationCommand cmd, const char *payload);

    void Status(http_status_t _status) {
        uint16_t status = uint16_t(_status);
        Packet(TranslationCommand::STATUS, &status, sizeof(status));
    }

    class ProcessorContext {
        TrafoResponse &response;

    public:
        ProcessorContext(TrafoResponse &_response):response(_response) {}

        void Container() {
            response.Packet(TranslationCommand::CONTAINER);
        }
    };

    ProcessorContext Process() {
        Packet(TranslationCommand::PROCESS);
        return ProcessorContext(*this);
    }

    class JailCGIContext {
        TrafoResponse &response;

    public:
        JailCGIContext(TrafoResponse &_response):response(_response) {}

        void JailCGI() {
            response.Packet(TranslationCommand::JAILCGI);
        }

        void Site(const char *value) {
            response.Packet(TranslationCommand::JAILCGI, value);
        }
    };

    class MountNamespaceContext {
        TrafoResponse &response;

    public:
        constexpr explicit
        MountNamespaceContext(TrafoResponse &_response):response(_response) {}

        void PivotRoot(const char *path) {
            return response.Packet(TranslationCommand::PIVOT_ROOT, path);
        }

        void MountProc() {
            return response.Packet(TranslationCommand::MOUNT_PROC);
        }

        void MountTmpTmpfs() {
            return response.Packet(TranslationCommand::MOUNT_TMP_TMPFS);
        }

        void MountHome(const char *mnt) {
            return response.Packet(TranslationCommand::MOUNT_HOME, mnt);
        }
    };

    class ChildContext {
        TrafoResponse &response;

    public:
        constexpr explicit
        ChildContext(TrafoResponse &_response):response(_response) {}

        JailCGIContext JailCGI() {
            response.Packet(TranslationCommand::JAILCGI);
            return JailCGIContext(response);
        }

        void Site(const char *value) {
            response.Packet(TranslationCommand::JAILCGI, value);
        }

        void Home(const char *value) {
            response.Packet(TranslationCommand::HOME, value);
        }

        void UserNamespace() {
            response.Packet(TranslationCommand::USER_NAMESPACE);
        }

        void PidNamespace() {
            response.Packet(TranslationCommand::PID_NAMESPACE);
        }

        void NetworkNamespace() {
            response.Packet(TranslationCommand::NETWORK_NAMESPACE);
        }

        void UtsNamespace() {
            response.Packet(TranslationCommand::PID_NAMESPACE);
        }

        MountNamespaceContext MountNamespace() {
            return MountNamespaceContext(response);
        }
    };

    class FileContext {
        TrafoResponse &response;

    public:
        FileContext(TrafoResponse &_response):response(_response) {}

        void ExpandPath(const char *value) {
            response.Packet(TranslationCommand::EXPAND_PATH, value);
        }

        void ContentType(const char *value) {
            response.Packet(TranslationCommand::CONTENT_TYPE, value);
        }

        void Deflated(const char *path) {
            response.Packet(TranslationCommand::DEFLATED, path);
        }

        void Gzipped(const char *path) {
            response.Packet(TranslationCommand::GZIPPED, path);
        }

        void DocumentRoot(const char *value) {
            response.Packet(TranslationCommand::DOCUMENT_ROOT, value);
        }

        ChildContext Delegate(const char *helper) {
            response.Packet(TranslationCommand::DELEGATE, helper);
            return ChildContext(response);
        }
    };

    FileContext Path(const char *path) {
        Packet(TranslationCommand::PATH, path);
        return FileContext(*this);
    }

    class HttpContext {
        TrafoResponse &response;

    public:
        constexpr explicit
        HttpContext(TrafoResponse &_response):response(_response) {}

        void ExpandPath(const char *value) {
            response.Packet(TranslationCommand::EXPAND_PATH, value);
        }

        void Address(const struct sockaddr *address, size_t length) {
            response.Packet(TranslationCommand::ADDRESS, address, length);
        }
    };

    HttpContext Http(const char *url) {
        Packet(TranslationCommand::HTTP, url);
        return HttpContext(*this);
    }

    WritableBuffer<uint8_t> Finish() {
        Packet(TranslationCommand::END);

        WritableBuffer<uint8_t> result(buffer, size);
        buffer = nullptr;
        capacity = size = 0;
        return result;
    }

private:
    void Grow(size_t new_capacity);
    void *Write(size_t nbytes);
};

#endif
