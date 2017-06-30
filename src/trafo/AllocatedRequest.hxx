/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_ALLOCATED_REQUEST_HXX
#define TRAFO_ALLOCATED_REQUEST_HXX

#include "Request.hxx"

#include <string>

enum class TranslationCommand : uint16_t;

class AllocatedTrafoRequest : public TrafoRequest {
    std::string uri_buffer, host_buffer;
    std::string args_buffer, query_string_buffer;

    std::string user_agent_buffer, ua_class_buffer;
    std::string accept_language_buffer;
    std::string authorization_buffer;

public:
    void Parse(TranslationCommand cmd,
               const void *payload, size_t length);
};

#endif
