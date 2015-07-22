/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NET_PARSER_HXX
#define NET_PARSER_HXX

class Error;
class AllocatedSocketAddress;

AllocatedSocketAddress
ParseSocketAddress(const char *p, int default_port, bool passive,
                   Error &error);

#endif
