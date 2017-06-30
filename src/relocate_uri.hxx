/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RELOCATE_HXX
#define BENG_PROXY_RELOCATE_HXX

#include "util/Compiler.h"

struct HttpAddress;
struct StringView;

gcc_pure
const char *
RelocateUri(struct pool &pool, const char *uri,
            const char *internal_host, StringView internal_path,
            const char *external_scheme, const char *external_host,
            StringView external_path,
            StringView base);

#endif
