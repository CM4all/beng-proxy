/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NET_PARSER_HXX
#define NET_PARSER_HXX

class AllocatedSocketAddress;

/**
 * Parse a numeric socket address.  Throws std::runtime_error on
 * error.
 */
AllocatedSocketAddress
ParseSocketAddress(const char *p, int default_port, bool passive);

#endif
