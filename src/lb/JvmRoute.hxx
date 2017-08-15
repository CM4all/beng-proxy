/*
 * Node selection by jvmRoute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_JVM_ROUTE_HXX
#define BENG_PROXY_LB_JVM_ROUTE_HXX

#include "StickyHash.hxx"

#include "util/Compiler.h"

class StringMap;
struct LbClusterConfig;

/**
 * Extract a jvm_route cookie from the request headers.
 */
gcc_pure
sticky_hash_t
lb_jvm_route_get(const StringMap &request_headers,
                 const LbClusterConfig &cluster);

#endif
