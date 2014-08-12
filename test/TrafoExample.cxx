/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "trafo/Framework.hxx"

#include <inline/compiler.h>

class ExampleHandler final : public TrafoFrameworkHandler {
public:
    void OnTrafoRequest(const TrafoRequest &request);
};

void
ExampleHandler::OnTrafoRequest(gcc_unused const TrafoRequest &request)
{
    TrafoResponse response;
    response.Status(HTTP_STATUS_NOT_FOUND);
    response.Process().Container();

    SendResponse(std::move(response));
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    return RunTrafo<ExampleHandler>();
}
