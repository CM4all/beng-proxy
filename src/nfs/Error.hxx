/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NFS_ERROR_HXX
#define NFS_ERROR_HXX

#include <stdexcept>

class NfsClientError : public std::runtime_error {
    int code;

public:
    explicit NfsClientError(const char *_msg)
        :std::runtime_error(_msg), code(0) {}

    NfsClientError(int _code, const char *_msg)
        :std::runtime_error(_msg), code(_code) {}

    NfsClientError(struct nfs_context *nfs, const char *msg);

    NfsClientError(int err, struct nfs_context *nfs, void *data,
                   const char *msg);

    int GetCode() const {
        return code;
    }
};

#endif
