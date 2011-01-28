/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROXY_WIDGET_H
#define __BENG_PROXY_WIDGET_H

#include "istream.h"

#include <http/status.h>

struct request;
struct widget;

void
proxy_widget(struct request *request2, http_status_t status, istream_t body,
             struct widget *widget,
             unsigned options);

#endif
