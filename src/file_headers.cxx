/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_headers.hxx"
#include "static_headers.hxx"
#include "GrowingBuffer.hxx"
#include "header_writer.hxx"
#include "format.h"
#include "http_date.hxx"
#include "request.hxx"
#include "http_server/Request.hxx"
#include "http_util.hxx"
#include "http_headers.hxx"
#include "translation/Vary.hxx"

#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef HAVE_ATTR_XATTR_H
#define NO_XATTR 1
#endif

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

static enum range_type
parse_range_header(const char *p, off_t *skip_r, off_t *size_r)
{
    unsigned long v;
    char *endptr;

    assert(p != nullptr);
    assert(skip_r != nullptr);
    assert(size_r != nullptr);

    if (memcmp(p, "bytes=", 6) != 0)
        return RANGE_INVALID;

    p += 6;

    if (*p == '-') {
        /* suffix-byte-range-spec */
        ++p;

        v = strtoul(p, &endptr, 10);
        if (v >= (unsigned long)*size_r)
            return RANGE_NONE;

        *skip_r = *size_r - v;
    } else {
        *skip_r = strtoul(p, &endptr, 10);
        if (*skip_r >= *size_r)
            return RANGE_INVALID;

        if (*endptr == '-') {
            p = endptr + 1;
            if (*p == 0)
                /* "wget -c" */
                return RANGE_VALID;

            v = strtoul(p, &endptr, 10);
            if (*endptr != 0 || v < (unsigned long)*skip_r ||
                v >= (unsigned long)*size_r)
                return RANGE_INVALID;

            *size_r = v + 1;
        }
    }

    return RANGE_VALID;
}

/**
 * Verifies the If-Range request header (RFC 2616 14.27).
 */
static bool
check_if_range(const char *if_range, const struct stat &st)
{
    if (if_range == nullptr)
        return true;

    const auto t = http_date_parse(if_range);
    if (t != std::chrono::system_clock::from_time_t(-1))
        return std::chrono::system_clock::from_time_t(st.st_mtime) == t;

    char etag[64];
    static_etag(etag, st);
    return strcmp(if_range, etag) == 0;
}

bool
file_evaluate_request(Request &request2,
                      int fd, const struct stat &st,
                      struct file_request &file_request)
{
    const auto &request = request2.request;
    const auto &request_headers = request.headers;
    const TranslateResponse *tr = request2.translate.response;
    const char *p;
    char buffer[64];

    if (tr->status == 0 && request.method == HTTP_METHOD_GET &&
        !request2.IsTransformationEnabled()) {
        p = request_headers.Get("range");

        if (p != nullptr &&
            check_if_range(request_headers.Get("if-range"), st))
            file_request.range =
                parse_range_header(p, &file_request.skip,
                                   &file_request.size);
    }

    if (!request2.IsProcessorEnabled()) {
        p = request_headers.Get("if-modified-since");
        if (p != nullptr) {
            const auto t = http_date_parse(p);
            if (t != std::chrono::system_clock::from_time_t(-1) &&
                std::chrono::system_clock::from_time_t(st.st_mtime) <= t) {
                HttpHeaders headers(request2.pool);
                GrowingBuffer headers2 = headers.MakeBuffer();

                if (fd >= 0)
                    file_cache_headers(headers2, fd, st,
                                       tr->expires_relative);

                write_translation_vary_header(headers2, *tr);

                response_dispatch(request2, HTTP_STATUS_NOT_MODIFIED,
                                  std::move(headers), nullptr);
                return false;
            }
        }

        p = request_headers.Get("if-unmodified-since");
        if (p != nullptr) {
            const auto t = http_date_parse(p);
            if (t != std::chrono::system_clock::from_time_t(-1) &&
                std::chrono::system_clock::from_time_t(st.st_mtime) > t) {
                response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                                  HttpHeaders(request2.pool), nullptr);
                return false;
            }
        }
    }

    if (!request2.IsTransformationEnabled()) {
        p = request_headers.Get("if-match");
        if (p != nullptr && strcmp(p, "*") != 0) {
            static_etag(buffer, st);

            if (!http_list_contains(p, buffer)) {
                response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                                  HttpHeaders(request2.pool), nullptr);
                return false;
            }
        }

        p = request_headers.Get("if-none-match");
        if (p != nullptr && strcmp(p, "*") == 0) {
            response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                              HttpHeaders(request2.pool), nullptr);
            return false;
        }

        if (p != nullptr) {
            static_etag(buffer, st);

            if (http_list_contains(p, buffer)) {
                response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                                  HttpHeaders(request2.pool), nullptr);
                return false;
            }
        }
    }

    return true;
}

gcc_pure
static std::chrono::seconds
read_xattr_max_age(int fd)
{
    assert(fd >= 0);

    char buffer[32];
    ssize_t nbytes = fgetxattr(fd, "user.MaxAge",
                               buffer, sizeof(buffer) - 1);
    if (nbytes <= 0)
        return std::chrono::seconds::zero();

    buffer[nbytes] = 0;

    char *endptr;
    unsigned long max_age = strtoul(buffer, &endptr, 10);
    if (*endptr != 0)
        return std::chrono::seconds::zero();

    return std::chrono::seconds(max_age);
}

static void
generate_expires(GrowingBuffer &headers,
                 std::chrono::system_clock::duration max_age)
{
    constexpr std::chrono::system_clock::duration max_max_age =
        std::chrono::hours(365 * 24);
    if (max_age > max_max_age)
        /* limit max_age to approximately one year */
        max_age = max_max_age;

    /* generate an "Expires" response header */
    header_write(headers, "expires",
                 http_date_format(std::chrono::system_clock::now() + max_age));
}

void
file_cache_headers(GrowingBuffer &headers,
                   int fd, const struct stat &st,
                   std::chrono::seconds max_age)
{
    assert(fd >= 0);

    char buffer[64];

#ifdef NO_XATTR
    (void)fd;
#endif

#ifndef NO_XATTR
    ssize_t nbytes;
    char etag[512];

    nbytes = fgetxattr(fd, "user.ETag",
                       etag + 1, sizeof(etag) - 3);
    if (nbytes > 0) {
        assert((size_t)nbytes < sizeof(etag));
        etag[0] = '"';
        etag[nbytes + 1] = '"';
        etag[nbytes + 2] = 0;
        header_write(headers, "etag", etag);
    } else {
#endif
        static_etag(buffer, st);
        header_write(headers, "etag", buffer);
#ifndef NO_XATTR
    }
#endif

#ifndef NO_XATTR
    if (max_age == std::chrono::seconds::zero())
        max_age = read_xattr_max_age(fd);
#endif
    if (max_age > std::chrono::seconds::zero())
        generate_expires(headers, max_age);
}

void
file_response_headers(GrowingBuffer &headers,
                      const char *override_content_type,
                      int fd, const struct stat &st,
                      std::chrono::seconds expires_relative,
                      bool processor_enabled, bool processor_first)
{
    if (!processor_first && fd >= 0)
        file_cache_headers(headers, fd, st, expires_relative);
    else {
        char etag[64];
        static_etag(etag, st);
        header_write(headers, "etag", etag);

        if (expires_relative > std::chrono::seconds::zero())
            generate_expires(headers, expires_relative);
    }

    if (override_content_type != nullptr) {
        /* content type override from the translation server */
        header_write(headers, "content-type", override_content_type);
    } else {
#ifndef NO_XATTR
        char content_type[256];
        if (load_xattr_content_type(content_type, sizeof(content_type), fd)) {
            header_write(headers, "content-type", content_type);
        } else {
#endif /* #ifndef NO_XATTR */
            header_write(headers, "content-type", "application/octet-stream");
#ifndef NO_XATTR
        }
#endif /* #ifndef NO_XATTR */
    }

#ifndef NO_LAST_MODIFIED_HEADER
    if (!processor_enabled)
        header_write(headers, "last-modified",
                     http_date_format(std::chrono::system_clock::from_time_t(st.st_mtime)));
#endif
}
