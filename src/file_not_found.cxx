/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_not_found.hxx"
#include "request.hxx"
#include "translate_response.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "lhttp_address.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>

gcc_pure
static bool
is_enoent(const char *path)
{
    struct stat st;
    return lstat(path, &st) < 0 && errno == ENOENT;
}

bool
check_file_not_found(Request &request,
                     const TranslateResponse &response)
{
    assert(!response.file_not_found.IsNull());

    if (response.test_path != nullptr) {
        if (!is_enoent(response.test_path))
            return true;
    } else {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::AJP:
        case ResourceAddress::Type::PIPE:
        case ResourceAddress::Type::NFS:
            daemon_log(2, "resource address not compatible with TRANSLATE_FILE_NOT_FOUND\n");
            response_dispatch_message(request, HTTP_STATUS_BAD_GATEWAY,
                                      "Internal Server Error");
            return false;

        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
            if (!is_enoent(response.address.u.cgi->path))
                return true;

            break;

        case ResourceAddress::Type::LHTTP:
            if (!is_enoent(response.address.u.lhttp->path))
                return true;

            break;

        case ResourceAddress::Type::LOCAL:
            if (!is_enoent(response.address.u.file->path))
                return true;

            break;

            // TODO: implement NFS
        }
    }

    if (++request.translate.n_file_not_found > 20) {
        daemon_log(2, "got too many consecutive FILE_NOT_FOUND packets\n");
        response_dispatch_message(request,
                                  HTTP_STATUS_BAD_GATEWAY,
                                  "Internal server error");
        return false;
    }

    request.translate.request.file_not_found = response.file_not_found;
    request.SubmitTranslateRequest();
    return false;
}
