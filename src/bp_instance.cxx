/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "nfs_cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi_stock.hxx"
#include "hstock.hxx"

void
instance::ForkCow(bool inherit)
{
    if (http_cache != nullptr)
        http_cache_fork_cow(*http_cache, inherit);

    if (filter_cache != nullptr)
        filter_cache_fork_cow(filter_cache, inherit);

    if (nfs_cache != nullptr)
        nfs_cache_fork_cow(nfs_cache, inherit);
}

void
instance::FadeChildren()
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
