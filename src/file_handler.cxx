/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_handler.hxx"
#include "file_headers.hxx"
#include "file_address.hxx"
#include "request.hxx"
#include "bp_instance.hxx"
#include "generate_response.hxx"
#include "header_writer.hxx"
#include "http/Date.hxx"
#include "http_util.hxx"
#include "http_headers.hxx"
#include "http_server/Request.hxx"
#include "istream/istream_file.hxx"
#include "istream/istream.hxx"
#include "translation/Vary.hxx"
#include "util/DecimalFormat.h"

#include <glib.h>

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

void
file_dispatch(Request &request2, const struct stat &st,
              const struct file_request &file_request,
              Istream *body)
{
    const TranslateResponse &tr = *request2.translate.response;
    const auto &address = *request2.handler.file.address;

    const char *override_content_type = request2.translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = address.content_type;

    HttpHeaders headers(request2.pool);
    GrowingBuffer &headers2 = headers.GetBuffer();
    file_response_headers(headers2, override_content_type,
                          istream_file_fd(*body), st,
                          tr.expires_relative,
                          request2.IsProcessorEnabled(),
                          request2.IsProcessorFirst());
    write_translation_vary_header(headers2, tr);

    http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;

    /* generate the Content-Range header */

    header_write(headers2, "accept-ranges", "bytes");

    switch (file_request.range) {
    case RANGE_NONE:
        break;

    case RANGE_VALID:
        istream_file_set_range(*body, file_request.skip,
                               file_request.size);

        assert(body->GetAvailable(false) ==
               file_request.size - file_request.skip);

        status = HTTP_STATUS_PARTIAL_CONTENT;

        header_write(headers2, "content-range",
                     p_sprintf(&request2.pool, "bytes %lu-%lu/%lu",
                               (unsigned long)file_request.skip,
                               (unsigned long)(file_request.size - 1),
                               (unsigned long)st.st_size));
        break;

    case RANGE_INVALID:
        status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        header_write(headers2, "content-range",
                     p_sprintf(&request2.pool, "bytes */%lu",
                               (unsigned long)st.st_size));

        body->CloseUnused();
        body = nullptr;
        break;
    }

    /* finished, dispatch this response */

    response_dispatch(request2, status, std::move(headers), body);
}

static bool
file_dispatch_compressed(Request &request2, const struct stat &st,
                         Istream &body, const char *encoding,
                         const char *path)
{
    const TranslateResponse &tr = *request2.translate.response;
    const auto &address = *request2.handler.file.address;

    /* open compressed file */

    struct stat st2;
    Istream *compressed_body =
        istream_file_stat_new(request2.instance.event_loop, request2.pool,
                              path, st2, nullptr);
    if (compressed_body == nullptr)
        return false;

    if (!S_ISREG(st2.st_mode)) {
        compressed_body->CloseUnused();
        return false;
    }

    /* response headers with information from uncompressed file */

    const char *override_content_type = request2.translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = address.content_type;

    HttpHeaders headers(request2.pool);
    GrowingBuffer &headers2 = headers.GetBuffer();
    file_response_headers(headers2, override_content_type,
                          istream_file_fd(body), st,
                          tr.expires_relative,
                          request2.IsProcessorEnabled(),
                          request2.IsProcessorFirst());
    write_translation_vary_header(headers2, tr);

    header_write(headers2, "content-encoding", encoding);
    header_write(headers2, "vary", "accept-encoding");

    /* close original file */

    body.CloseUnused();

    /* finished, dispatch this response */

    request2.compressed = true;

    http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;
    response_dispatch(request2, status, std::move(headers), compressed_body);
    return true;
}

static bool
file_check_compressed(Request &request2, const struct stat &st,
                      Istream &body, const char *encoding,
                      const char *path)
{
    const auto &request = request2.request;

    return path != nullptr &&
        http_client_accepts_encoding(request.headers, encoding) &&
        file_dispatch_compressed(request2, st, body, encoding, path);
}

static bool
file_check_auto_compressed(Request &request2, const struct stat &st,
                           Istream &body, const char *encoding,
                           const char *path, const char *suffix)
{
    assert(encoding != nullptr);
    assert(path != nullptr);
    assert(suffix != nullptr);
    assert(*suffix == '.');
    assert(suffix[1] != 0);

    const auto &request = request2.request;

    if (!http_client_accepts_encoding(request.headers, encoding))
        return false;

    const char *compressed_path = p_strcat(&request2.pool, path, suffix,
                                           nullptr);

    return file_dispatch_compressed(request2, st, body, encoding,
                                    compressed_path);
}

void
file_callback(Request &request2, const FileAddress &address)
{
    request2.handler.file.address = &address;

    const auto &request = request2.request;
    struct file_request file_request;

    assert(address.path != nullptr);
    assert(address.delegate == nullptr);

    const char *const path = address.path;

    /* check request */

    if (request.method != HTTP_METHOD_HEAD &&
        request.method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* open the file */

    GError *error = nullptr;
    struct stat st;
    Istream *body = istream_file_stat_new(request2.instance.event_loop,
                                          request2.pool,
                                          path, st, &error);
    if (body == nullptr) {
        response_dispatch_error(request2, error);
        g_error_free(error);
        return;
    }

    /* check file type */

    if (S_ISCHR(st.st_mode)) {
        /* allow character devices, but skip range etc. */
        response_dispatch(request2, HTTP_STATUS_OK, HttpHeaders(request2.pool),
                          body);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        body->CloseUnused();
        response_dispatch_message(request2, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Not a regular file");
        return;
    }

    file_request.size = st.st_size;

    /* request options */

    if (!file_evaluate_request(request2, istream_file_fd(*body), st,
                               file_request)) {
        body->CloseUnused();
        return;
    }

    /* precompressed? */

    if (!request2.compressed &&
        file_request.range == RANGE_NONE &&
        !request2.IsTransformationEnabled() &&
        (file_check_compressed(request2, st, *body, "deflate",
                               address.deflated) ||
         (address.auto_gzipped &&
          file_check_auto_compressed(request2, st, *body, "gzip",
                                     address.path, ".gz")) ||
         file_check_compressed(request2, st, *body, "gzip",
                               address.gzipped)))
        return;

    /* build the response */

    file_dispatch(request2, st, file_request, body);
}
