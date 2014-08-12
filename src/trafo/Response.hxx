/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_RESPONSE_HXX
#define TRAFO_RESPONSE_HXX

#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <beng-proxy/translation.h>
#include <http/status.h>

#include <stdint.h>
#include <stddef.h>

class TrafoResponse {
    uint8_t *buffer;
    size_t capacity, size;

public:
    TrafoResponse()
        :buffer(nullptr), capacity(0), size(0) {
        Packet(TRANSLATE_BEGIN);
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
    void Packet(beng_translation_command cmd);

    void Packet(beng_translation_command cmd, ConstBuffer<void> payload);

    void Packet(beng_translation_command cmd,
                const void *payload, size_t length) {
        Packet(cmd, {payload, length});
    }

    void Packet(beng_translation_command cmd, const char *payload);

    void Status(http_status_t _status) {
        uint16_t status = uint16_t(_status);
        Packet(TRANSLATE_STATUS, &status, sizeof(status));
    }

    class ProcessorContext {
        TrafoResponse &response;

    public:
        ProcessorContext(TrafoResponse &_response):response(_response) {}

        void Container() {
            response.Packet(TRANSLATE_CONTAINER);
        }
    };

    ProcessorContext Process() {
        Packet(TRANSLATE_PROCESS);
        return ProcessorContext(*this);
    }

    class JailCGIContext {
        TrafoResponse &response;

    public:
        JailCGIContext(TrafoResponse &_response):response(_response) {}

        void JailCGI() {
            response.Packet(TRANSLATE_JAILCGI);
        }

        void Site(const char *value) {
            response.Packet(TRANSLATE_JAILCGI, value);
        }
    };

    class MountNamespaceContext {
        TrafoResponse &response;

    public:
        constexpr explicit
        MountNamespaceContext(TrafoResponse &_response):response(_response) {}

        void PivotRoot(const char *path) {
            return response.Packet(TRANSLATE_PIVOT_ROOT, path);
        }

        void MountProc() {
            return response.Packet(TRANSLATE_MOUNT_PROC);
        }

        void MountTmpTmpfs() {
            return response.Packet(TRANSLATE_MOUNT_TMP_TMPFS);
        }

        void MountHome(const char *mnt) {
            return response.Packet(TRANSLATE_MOUNT_HOME, mnt);
        }
    };

    class ChildContext {
        TrafoResponse &response;

    public:
        constexpr explicit
        ChildContext(TrafoResponse &_response):response(_response) {}

        JailCGIContext JailCGI() {
            response.Packet(TRANSLATE_JAILCGI);
            return JailCGIContext(response);
        }

        void Site(const char *value) {
            response.Packet(TRANSLATE_JAILCGI, value);
        }

        void Home(const char *value) {
            response.Packet(TRANSLATE_HOME, value);
        }

        void UserNamespace() {
            response.Packet(TRANSLATE_USER_NAMESPACE);
        }

        void PidNamespace() {
            response.Packet(TRANSLATE_PID_NAMESPACE);
        }

        void NetworkNamespace() {
            response.Packet(TRANSLATE_NETWORK_NAMESPACE);
        }

        void UtsNamespace() {
            response.Packet(TRANSLATE_PID_NAMESPACE);
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
            response.Packet(TRANSLATE_EXPAND_PATH, value);
        }

        void ContentType(const char *value) {
            response.Packet(TRANSLATE_CONTENT_TYPE, value);
        }

        void Deflated(const char *path) {
            response.Packet(TRANSLATE_DEFLATED, path);
        }

        void Gzipped(const char *path) {
            response.Packet(TRANSLATE_GZIPPED, path);
        }

        void DocumentRoot(const char *value) {
            response.Packet(TRANSLATE_DOCUMENT_ROOT, value);
        }

        ChildContext Delegate(const char *helper) {
            response.Packet(TRANSLATE_DELEGATE, helper);
            return ChildContext(response);
        }
    };

    FileContext Path(const char *path) {
        Packet(TRANSLATE_PATH, path);
        return FileContext(*this);
    }

    class HttpContext {
        TrafoResponse &response;

    public:
        constexpr explicit
        HttpContext(TrafoResponse &_response):response(_response) {}

        void ExpandPath(const char *value) {
            response.Packet(TRANSLATE_EXPAND_PATH, value);
        }

        void Address(const struct sockaddr *address, size_t length) {
            response.Packet(TRANSLATE_ADDRESS, address, length);
        }
    };

    HttpContext Http(const char *url) {
        Packet(TRANSLATE_HTTP, url);
        return HttpContext(*this);
    }

    WritableBuffer<uint8_t> Finish() {
        Packet(TRANSLATE_END);

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
