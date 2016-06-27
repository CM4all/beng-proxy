/*
 * Node selection by jvmRoute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_JVM_ROUTE_H
#define BENG_PROXY_LB_JVM_ROUTE_H

#include <inline/compiler.h>

class StringMap;
struct LbClusterConfig;

/**
 * Extract a jvm_route cookie from the request headers.
 */
gcc_pure
unsigned
lb_jvm_route_get(const StringMap &request_headers,
                 const LbClusterConfig &cluster);

#endif
