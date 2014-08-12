/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_ALLOCATED_REQUEST_HXX
#define TRAFO_ALLOCATED_REQUEST_HXX

#include <beng-proxy/translation.h>

#include "Event.hxx"
#include "util/DynamicFifoBuffer.hxx"
#include "Request.hxx"

#include <string>

#include <stdint.h>

class AllocatedTrafoRequest : public TrafoRequest {
    std::string uri_buffer, host_buffer;
    std::string args_buffer, query_string_buffer;

    std::string user_agent_buffer, ua_class_buffer;
    std::string accept_language_buffer;
    std::string authorization_buffer;

public:
    void Parse(beng_translation_command cmd,
               const void *payload, size_t length);
};

#endif
