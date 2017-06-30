/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UA_CLASSIFICATION_HXX
#define BENG_PROXY_UA_CLASSIFICATION_HXX

#include "util/Compiler.h"

/**
 * Throws std::runtime_error on error.
 */
void
ua_classification_init(const char *path);

void
ua_classification_deinit();

gcc_pure
const char *
ua_classification_lookup(const char *user_agent);

#endif
