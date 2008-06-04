/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CONFIG_H
#define __BENG_CONFIG_H

#include <sys/types.h>
#include <stdbool.h>

#ifdef NDEBUG
static const bool debug_mode = false;
#else
extern bool debug_mode;
#endif

struct config {
    unsigned port;

    const char *document_root;

    const char *translation_socket;

    unsigned num_workers;

    /** maximum number of simultaneous connections */
    unsigned max_connections;
};

void
parse_cmdline(struct config *config, int argc, char **argv);

#endif
