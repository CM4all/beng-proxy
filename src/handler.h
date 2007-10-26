/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HANDLER_H
#define __BENG_HANDLER_H

struct parsed_uri;
struct http_server_request;
struct translate_response;

extern const struct http_server_connection_handler my_http_server_connection_handler;

void
file_callback(struct http_server_request *request,
              const struct parsed_uri *uri,
              const struct translate_response *tr);

void
proxy_callback(struct http_server_request *request,
               const struct parsed_uri *external_uri,
               const struct translate_response *tr);

#endif
