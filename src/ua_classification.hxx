/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UA_CLASSIFICATION_HXX
#define BENG_PROXY_UA_CLASSIFICATION_HXX

#include <inline/compiler.h>

class Error;

bool
ua_classification_init(const char *path, Error &error);

void
ua_classification_deinit();

gcc_pure
const char *
ua_classification_lookup(const char *user_agent);

#endif
