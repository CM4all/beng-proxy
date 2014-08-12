/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_HANDLER_HXX
#define TRAFO_HANDLER_HXX

struct TrafoRequest;
class TrafoConnection;

class TrafoHandler {
public:
    virtual void OnTrafoRequest(TrafoConnection &connection,
                                const TrafoRequest &request) = 0;
};

#endif
