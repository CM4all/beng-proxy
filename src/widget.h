/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_H
#define __BENG_WIDGET_H

struct widget {
    /** the widget's instance id, as specified in the template */
    const char *id;

    /** the base URI of this widget, as specified in the template */
    const char *base_uri;

    /** the URI which is actually retrieved - this is the same as
        base_uri, except when the user clicked on a relative link */
    const char *real_uri;
};

#endif
