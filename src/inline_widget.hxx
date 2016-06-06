/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INLINE_WIDGET_HXX
#define BENG_PROXY_INLINE_WIDGET_HXX

struct pool;
class Istream;
struct Widget;
struct processor_env;

extern const struct timeval inline_widget_timeout;

/**
 * Utility function for the HTML processor which prepares a widget for
 * inlining into a HTML template.
 *
 * It requests the specified widget and formats the response in a way
 * that is suitable for embedding in HTML.
 *
 * @param plain_text expect text/plain?
 */
Istream *
embed_inline_widget(struct pool &pool, struct processor_env &env,
                    bool plain_text,
                    Widget &widget);

#endif
