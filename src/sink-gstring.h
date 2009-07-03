#ifndef SINK_GSTRING_H
#define SINK_GSTRING_H

#include "istream.h"

#include <glib.h>

struct sink_gstring {
    GString *value;
    bool finished;
};

struct sink_gstring *
sink_gstring_new(pool_t pool, istream_t istream);

#endif
