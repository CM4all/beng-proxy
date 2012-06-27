/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_APPROVAL_H
#define BENG_PROXY_WIDGET_APPROVAL_H

#include <inline/compiler.h>

#include <stdbool.h>

struct widget;

bool
widget_init_approval(struct widget *widget, bool self_container);

/**
 * Check the "approval" value.  If it is #WIDGET_APPROVAL_UNKNOWN,
 * check the widget group approval of the parent widget.  This is a
 * postponed check because a widget's group is only known after its
 * widget class has been looked up.
 *
 * @return true if the widget is approved
 */
gcc_pure
bool
widget_check_approval(struct widget *widget);

#endif
