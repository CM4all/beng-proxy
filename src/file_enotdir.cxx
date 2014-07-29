/*
 * Implementation of TRANSLATE_ENOTDIR.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_enotdir.hxx"
#include "request.hxx"
#include "translate_response.hxx"
#include "resource_address.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "lhttp_address.hxx"
#include "http_server.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

gcc_pure
static const char *
get_file_path(const TranslateResponse &response)
{
    if (response.test_path != nullptr)
        return response.test_path;

    switch (response.address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return nullptr;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return response.address.u.cgi->path;

    case RESOURCE_ADDRESS_LHTTP:
        return response.address.u.lhttp->path;

    case RESOURCE_ADDRESS_LOCAL:
        return response.address.u.file->path;

        // TODO: implement NFS
    }
}

static bool
submit_enotdir(struct request &request, const TranslateResponse &response)
{
    request.translate.request.enotdir = response.enotdir;

    const char *const uri = request.request->uri;
    if (request.translate.enotdir_uri == nullptr) {
        request.translate.request.uri = request.translate.enotdir_uri =
            p_strdup(request.request->pool, uri);
        request.translate.enotdir_path_info = uri + strlen(uri);
    }

    const char *slash = (const char *)
        memrchr(uri, '/', request.translate.enotdir_path_info - uri);
    if (slash == nullptr || slash == uri)
        return true;

    request.translate.enotdir_uri[slash - uri] = 0;
    request.translate.enotdir_path_info = slash;

    request.SubmitTranslateRequest();
    return false;
}

bool
check_file_enotdir(struct request &request,
                   const TranslateResponse &response)
{
    assert(!response.enotdir.IsNull());

    const char *path = get_file_path(response);
    if (path == nullptr) {
        daemon_log(2, "resource address not compatible with ENOTDIR\n");
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal Server Error");
        return false;
    }

    struct stat st;
    if (stat(path, &st) < 0 && errno == ENOTDIR)
        return submit_enotdir(request, response);

    return true;
}

void
apply_file_enotdir(struct request &request)
{
    if (request.translate.enotdir_path_info != nullptr) {
        /* append the path_info to the resource address */

        auto address =
            resource_address_apply(request.request->pool,
                                   request.translate.address,
                                   request.translate.enotdir_path_info,
                                   strlen(request.translate.enotdir_path_info),
                                   &request.translate.enotdir_address);
        if (address != nullptr)
            request.translate.address = address;
    }
}
