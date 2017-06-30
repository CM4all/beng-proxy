/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PROCESSOR_HXX
#define PROCESSOR_HXX

#include "util/Compiler.h"
#include <http/method.h>
#include <http/status.h>

#include <stdbool.h>

/** options for processor_new() */
enum processor_options {
    /** rewrite URLs */
    PROCESSOR_REWRITE_URL = 0x1,

    /** add prefix to marked CSS class names */
    PROCESSOR_PREFIX_CSS_CLASS = 0x2,

    /**
     * Default URI rewrite mode is base=widget mode=focus.
     */
    PROCESSOR_FOCUS_WIDGET = 0x4,

    /** add prefix to marked XML ids */
    PROCESSOR_PREFIX_XML_ID = 0x8,

    /** enable the c:embed element */
    PROCESSOR_CONTAINER = 0x10,

    /**
     * Invoke the CSS processor for "style" element contents?
     */
    PROCESSOR_STYLE = 0x20,

    /**
     * Allow this widget to embed more instances of its own class.
     */
    PROCESSOR_SELF_CONTAINER = 0x40,
};

struct pool;
class Istream;
struct parsed_uri;
struct Widget;
struct processor_env;
class StringMap;
class WidgetLookupHandler;
class CancellablePointer;

gcc_pure
bool
processable(const StringMap &headers);

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
Istream *
processor_process(struct pool &pool, Istream &istream,
                  Widget &widget,
                  struct processor_env &env,
                  unsigned options);

/**
 * Process the specified istream, and find the specified widget.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
processor_lookup_widget(struct pool &pool, Istream &istream,
                        Widget &widget, const char *id,
                        struct processor_env &env,
                        unsigned options,
                        WidgetLookupHandler &handler,
                        CancellablePointer &cancel_ptr);

#endif
