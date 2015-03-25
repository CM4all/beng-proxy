/*
 * Implementation of --check.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CHECK_HXX
#define BENG_PROXY_LB_CHECK_HXX

class Error;

bool
lb_check(const struct lb_config &config, Error &error);

#endif
