/*
 * Node selection by jvmRoute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_jvm_route.hxx"
#include "lb_config.hxx"
#include "strmap.hxx"
#include "tpool.h"
#include "cookie_server.hxx"
#include "pool.hxx"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

unsigned
lb_jvm_route_get(const struct strmap *request_headers,
                 const struct lb_cluster_config *cluster)
{
    const AutoRewindPool auto_rewind(*tpool);

    const char *cookie = request_headers->Get("cookie");
    if (cookie == NULL)
        return 0;

    struct strmap *jar = strmap_new(tpool);
    cookie_map_parse(jar, cookie, tpool);

    const char *p = jar->Get("JSESSIONID");
    if (p == NULL)
        return 0;

    p = strchr(p, '.');
    if (p == NULL || p[1] == 0)
        return 0;

    const char *jvm_route = p + 1;
    int i = cluster->FindJVMRoute(jvm_route);
    if (i < 0)
        return 0;

    /* add num_members to make sure that the modulo still maps to the
       node index, but the first node is not referred to as zero
       (special value for "no session") */
    return i + cluster->members.size();
}
