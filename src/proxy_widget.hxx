/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PROXY_WIDGET_HXX
#define BENG_PROXY_PROXY_WIDGET_HXX

#include <http/status.h>

struct istream;
struct request;
struct widget;
struct widget_ref;

void
proxy_widget(struct request &request2,
             struct istream *body,
             struct widget *widget, const struct widget_ref *proxy_ref,
             unsigned options);

#endif
