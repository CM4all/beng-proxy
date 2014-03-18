/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_not_found.hxx"
#include "request.hxx"
#include "translate_response.hxx"
#include "resource-address.h"
#include "file_address.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>

gcc_pure
static bool
is_enoent(const char *path)
{
    struct stat st;
    return stat(path, &st) < 0 && errno == ENOENT;
}

bool
check_file_not_found(struct request &request,
                     const TranslateResponse &response)
{
    assert(!response.file_not_found.IsNull());

    switch (response.address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
    case RESOURCE_ADDRESS_NFS:
        daemon_log(2, "resource address not compatible with TRANSLATE_FILE_NOT_FOUND\n");
        response_dispatch_message(&request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal Server Error");
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        if (!is_enoent(response.address.u.file->path))
            return true;

        break;

        // TODO: implement NFS
    }

    if (++request.translate.n_file_not_found > 4) {
        daemon_log(2, "got too many consecutive FILE_NOT_FOUND packets\n");
        response_dispatch_message(&request,
                                  HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return false;
    }

    request.translate.request.file_not_found = response.file_not_found;
    request.SubmitTranslateRequest();
    return false;
}
