/*
 * OpenSSL BIO_s_mem() utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_MEM_BIO_HXX
#define BENG_PROXY_SSL_MEM_BIO_HXX

#include "Error.hxx"
#include "Unique.hxx"
#include "util/AllocatedString.hxx"

/**
 * Call a function that writes into a memory BIO and return the BIO
 * memory as #AllocatedString instance.
 */
template<typename W>
static inline AllocatedString<>
BioWriterToString(W &&writer)
{
    UniqueBIO bio(BIO_new(BIO_s_mem()));
    if (bio == nullptr)
        throw SslError("BIO_new() failed");

    writer(*bio);

    char *data;
    long length = BIO_get_mem_data(bio.get(), &data);
    return AllocatedString<>::Duplicate(data, length);
}

#endif
