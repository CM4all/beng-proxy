/*
 * Interface for Content-Types managed by the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SUFFIX_REGISTRY_HXX
#define BENG_PROXY_SUFFIX_REGISTRY_HXX

#include "glibfwd.hxx"

struct pool;
struct tcache;
struct async_operation_ref;
template<typename T> struct ConstBuffer;
struct Transformation;

struct SuffixRegistryHandler {
    /**
     * @param transformations an optional #Transformation chain for
     * all files of this type
     */
    void (*success)(const char *content_type,
                    const Transformation *transformations,
                    void *ctx);

    void (*error)(GError *error, void *ctx);
};
typedef void (*widget_class_callback_t)(const struct widget_class *cls,
                                        void *ctx);

void
suffix_registry_lookup(struct pool *pool,
                       struct tcache &tcache,
                       ConstBuffer<void> payload,
                       const char *suffix,
                       const SuffixRegistryHandler &handler, void *ctx,
                       struct async_operation_ref *async_ref);

#endif
