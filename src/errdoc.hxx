/*
 * Error document handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ERRDOC_HXX
#define BENG_PROXY_ERRDOC_HXX

#include <http/status.h>

struct istream;
struct request;
class HttpHeaders;
template<typename T> struct ConstBuffer;

/**
 * Asks the translation server for an error document, and submits it
 * to response_dispatch().  If there is no error document, or the
 * error document resource fails, it resubmits the original response.
 *
 * @param error_document the payload of the #TRANSLATE_ERROR_DOCUMENT
 * translate response packet
 */
void
errdoc_dispatch_response(struct request &request2, http_status_t status,
                         ConstBuffer<void> error_document,
                         HttpHeaders &&headers, struct istream *body);

#endif
