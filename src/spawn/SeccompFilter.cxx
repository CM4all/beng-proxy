/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SeccompFilter.hxx"

SeccompFilter::SeccompFilter(uint32_t def_action)
    :ctx(seccomp_init(def_action))
{
    if (ctx == nullptr)
        throw std::runtime_error("seccomp_init() failed");
}

void
SeccompFilter::Reset(uint32_t def_action)
{
    if (seccomp_reset(ctx, def_action) < 0)
        throw std::runtime_error("seccomp_reset() failed");
}

void
SeccompFilter::Load() const
{
    if (seccomp_load(ctx) < 0)
        throw std::runtime_error("seccomp_load() failed");
}
