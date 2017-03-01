/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Loop.hxx"

void
EventLoop::Defer(DeferEvent &e)
{
    defer.push_front(e);
}

void
EventLoop::CancelDefer(DeferEvent &e)
{
    defer.erase(defer.iterator_to(e));
}

bool
EventLoop::RunDeferred()
{
    while (!defer.empty())
        defer.pop_front_and_dispose([](DeferEvent *e){
                e->OnDeferred();
            });

    return true;
}
