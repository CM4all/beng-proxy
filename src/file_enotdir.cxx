/*
 * Implementation of TRANSLATE_ENOTDIR.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_enotdir.hxx"
#include "request.hxx"
#include "translate_response.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "lhttp_address.hxx"
#include "http_server.hxx"
#include "pool.hxx"

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
    case ResourceAddress::Type::NONE:
    case ResourceAddress::Type::HTTP:
    case ResourceAddress::Type::AJP:
    case ResourceAddress::Type::PIPE:
    case ResourceAddress::Type::NFS:
        return nullptr;

    case ResourceAddress::Type::CGI:
    case ResourceAddress::Type::FASTCGI:
    case ResourceAddress::Type::WAS:
        return response.address.u.cgi->path;

    case ResourceAddress::Type::LHTTP:
        return response.address.u.lhttp->path;

    case ResourceAddress::Type::LOCAL:
        return response.address.u.file->path;

        // TODO: implement NFS
    }
}

static bool
submit_enotdir(Request &request, const TranslateResponse &response)
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
check_file_enotdir(Request &request,
                   const TranslateResponse &response)
{
    assert(!response.enotdir.IsNull());

    const char *path = get_file_path(response);
    if (path == nullptr) {
        response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                              "Resource address not compatible with ENOTDIR");
        return false;
    }

    struct stat st;
    if (stat(path, &st) < 0 && errno == ENOTDIR)
        return submit_enotdir(request, response);

    return true;
}

void
apply_file_enotdir(Request &request)
{
    if (request.translate.enotdir_path_info != nullptr) {
        /* append the path_info to the resource address */

        auto address =
            request.translate.address->Apply(*request.request->pool,
                                             request.translate.enotdir_path_info,
                                             strlen(request.translate.enotdir_path_info),
                                             request.translate.enotdir_address);
        if (address != nullptr)
            request.translate.address = address;
    }
}
