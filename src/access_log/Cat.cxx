/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"
#include "OneLine.hxx"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    AccessLogServer(0).Run(LogOneLine);
    return 0;
}
