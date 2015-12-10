/*
 * OpenSSL error reporting.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Error.hxx"
#include "MemBio.hxx"

#include <openssl/err.h>

static AllocatedString<>
ErrToString()
{
    return BioWriterToString([](BIO &bio){
            ERR_print_errors(&bio);
        });
}

SslError::SslError(const std::string &msg)
    :std::runtime_error(msg + ": " + ErrToString().c_str()) {}
