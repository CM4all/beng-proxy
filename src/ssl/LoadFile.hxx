/*
 * Load OpenSSL objects from files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_LOAD_FILE_HXX
#define BENG_PROXY_SSL_LOAD_FILE_HXX

#include "Unique.hxx"

#include <forward_list>

UniqueX509
LoadCertFile(const char *path);

std::forward_list<UniqueX509>
LoadCertChainFile(const char *path);

UniqueEVP_PKEY
LoadKeyFile(const char *path);

#endif
