/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INLINE_WIDGET_HXX
#define BENG_PROXY_INLINE_WIDGET_HXX

struct pool;
struct widget;
struct processor_env;

/**
 * Utility function for the HTML processor which prepares a widget for
 * inlining into a HTML template.
 *
 * It requests the specified widget and formats the response in a way
 * that is suitable for embedding in HTML.
 *
 * @param plain_text expect text/plain?
 */
struct istream *
embed_inline_widget(struct pool *pool, struct processor_env *env,
                    bool plain_text,
                    struct widget *widget);

#endif
