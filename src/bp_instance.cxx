/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "fb_pool.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "tcache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "stock/MapStock.hxx"
#include "event/Callback.hxx"

#ifdef HAVE_LIBNFS
#include "nfs_cache.hxx"
#endif

BpInstance::BpInstance()
    :shutdown_listener(ShutdownCallback, this),
     respawn_trigger(MakeSimpleEventCallback(BpInstance,
                                             RespawnWorkerCallback),
                     this, 1)
{
    list_init(&connections);
    list_init(&workers);
}

void
BpInstance::ForkCow(bool inherit)
{
    fb_pool_fork_cow(inherit);

    if (translate_cache != nullptr)
        translate_cache_fork_cow(*translate_cache, inherit);

    if (http_cache != nullptr)
        http_cache_fork_cow(*http_cache, inherit);

    if (filter_cache != nullptr)
        filter_cache_fork_cow(filter_cache, inherit);

#ifdef HAVE_LIBNFS
    if (nfs_cache != nullptr)
        nfs_cache_fork_cow(*nfs_cache, inherit);
#endif
}

void
BpInstance::FadeChildren()
{
    if (lhttp_stock != nullptr)
        lhttp_stock_fade_all(*lhttp_stock);

    if (fcgi_stock != nullptr)
        fcgi_stock_fade_all(*fcgi_stock);

    if (was_stock != nullptr)
        hstock_fade_all(*was_stock);

    if (delegate_stock != nullptr)
        hstock_fade_all(*delegate_stock);
}
