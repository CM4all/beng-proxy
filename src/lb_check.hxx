/*
 * Implementation of --check.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CHECK_HXX
#define BENG_PROXY_LB_CHECK_HXX

struct LbConfig;

void
lb_check(const LbConfig &config);

#endif
