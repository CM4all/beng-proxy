/*
 * Implementation of TRANSLATE_DIRECTORY_INDEX.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_directory_index.hxx"
#include "request.hxx"
#include "translate_response.hxx"
#include "resource_address.hxx"
#include "file_address.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>

gcc_pure
static bool
is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool
check_directory_index(struct request &request,
                           const TranslateResponse &response)
{
    assert(!response.directory_index.IsNull());

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
        daemon_log(2, "resource address not compatible with TRANSLATE_DIRECTORY_INDEX\n");
        response_dispatch_message(&request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal Server Error");
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        if (!is_dir(response.address.u.file->path))
            return true;

        break;

        // TODO: implement NFS
    }

    if (++request.translate.n_directory_index > 4) {
        daemon_log(2, "got too many consecutive DIRECTORY_INDEX packets\n");
        response_dispatch_message(&request,
                                  HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return false;
    }

    request.translate.request.directory_index = response.directory_index;
    request.SubmitTranslateRequest();
    return false;
}
