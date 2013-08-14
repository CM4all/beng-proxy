/*
 * Node selection by jvmRoute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_jvm_route.h"
#include "lb_config.h"
#include "strmap.h"
#include "tpool.h"
#include "cookie_server.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

G_GNUC_PURE
static unsigned
lb_jvm_route_get_internal(const struct strmap *request_headers,
                          const struct lb_cluster_config *cluster)
{
    const char *cookie = strmap_get(request_headers, "cookie");
    if (cookie == NULL)
        return 0;

    struct strmap *jar = strmap_new(tpool, 31);
    cookie_map_parse(jar, cookie, tpool);

    const char *p = strmap_get(jar, "JSESSIONID");
    if (p == NULL)
        return 0;

    p = strchr(p, '.');
    if (p == NULL || p[1] == 0)
        return 0;

    const char *jvm_route = p + 1;
    int i = lb_config_find_jvm_route(cluster, jvm_route);
    if (i < 0)
        return 0;

    /* add num_members to make sure that the modulo still maps to the
       node index, but the first node is not referred to as zero
       (special value for "no session") */
    return i + cluster->num_members;
}

unsigned
lb_jvm_route_get(const struct strmap *request_headers,
                 const struct lb_cluster_config *cluster)
{
    struct pool_mark_state mark;
    pool_mark(tpool, &mark);

    unsigned id = lb_jvm_route_get_internal(request_headers, cluster);
    pool_rewind(tpool, &mark);

    return id;
}
