/*
 * Interface for Content-Types managed by the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_SUFFIX_REGISTRY_HXX
#define BENG_PROXY_ADDRESS_SUFFIX_REGISTRY_HXX

#include "glibfwd.hxx"

struct pool;
struct tcache;
struct resource_address;
struct SuffixRegistryHandler;
struct async_operation_ref;

bool
suffix_registry_lookup(struct pool &pool, struct tcache &translate_cache,
                       const struct resource_address &address,
                       const SuffixRegistryHandler &handler, void *ctx,
                       struct async_operation_ref &async_ref);

#endif
