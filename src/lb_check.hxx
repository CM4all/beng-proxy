/*
 * Implementation of --check.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CHECK_HXX
#define BENG_PROXY_LB_CHECK_HXX

class Error;
struct LbConfig;

bool
lb_check(const LbConfig &config, Error &error);

#endif
