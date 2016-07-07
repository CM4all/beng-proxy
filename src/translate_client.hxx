/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_CLIENT_HXX
#define BENG_PROXY_TRANSLATE_CLIENT_HXX

struct pool;
class EventLoop;
class Lease;
class CancellablePointer;
struct TranslateRequest;
struct TranslateHandler;

void
translate(struct pool &pool, EventLoop &event_loop,
          int fd, Lease &lease,
          const TranslateRequest &request,
          const TranslateHandler &handler, void *ctx,
          CancellablePointer &cancel_ptr);

#endif
