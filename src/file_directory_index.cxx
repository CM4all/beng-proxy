/*
 * Implementation of TRANSLATE_DIRECTORY_INDEX.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_directory_index.hxx"
#include "request.hxx"
#include "translation/Response.hxx"
#include "file_address.hxx"

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
check_directory_index(Request &request,
                      const TranslateResponse &response)
{
    assert(!response.directory_index.IsNull());

    if (response.test_path != nullptr) {
        if (!is_dir(response.test_path))
            return true;
    } else {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::LHTTP:
        case ResourceAddress::Type::PIPE:
        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
        case ResourceAddress::Type::NFS:
            response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                                  "Resource address not compatible with DIRECTORY_INDEX");
            return false;

        case ResourceAddress::Type::LOCAL:
            if (!is_dir(response.address.GetFile().path))
                return true;

            break;

            // TODO: implement NFS
        }
    }

    if (++request.translate.n_directory_index > 4) {
        response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                              "Got too many consecutive DIRECTORY_INDEX packets");
        return false;
    }

    request.translate.request.directory_index = response.directory_index;
    request.SubmitTranslateRequest();
    return false;
}
