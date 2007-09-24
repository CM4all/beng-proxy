/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CONFIG_H
#define __BENG_CONFIG_H

#include <sys/types.h>

#ifdef NDEBUG
static const int debug_mode = 0;
#else
extern int debug_mode;
#endif

struct config {
    const char *document_root;
};

void
parse_cmdline(struct config *config, int argc, char **argv);

#endif
