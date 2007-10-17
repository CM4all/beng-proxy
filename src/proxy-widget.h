/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROXY_WIDGET_H
#define __BENG_PROXY_WIDGET_H

#include "http-server.h"
#include "processor.h"

void
widget_proxy_install(struct processor_env *env,
                     struct http_server_request *request,
                     istream_t body);

#endif
