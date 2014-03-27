/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_approval.hxx"
#include "widget-class.h"
#include "widget.h"

bool
widget_init_approval(struct widget *widget, bool self_container)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);
    assert(widget->approval == widget::WIDGET_APPROVAL_GIVEN);

    const struct widget *parent = widget->parent;

    if (!self_container) {
        if (widget_class_has_groups(parent->cls))
            /* the container limits the groups; postpone a check until
               we know the widget's group */
            widget->approval = widget::WIDGET_APPROVAL_UNKNOWN;

        return true;
    }

    if (parent->class_name != NULL &&
        strcmp(parent->class_name, widget->class_name) == 0)
        /* approved by SELF_CONTAINER */
        return true;

    /* failed the SELF_CONTAINER test */

    if (widget_class_has_groups(parent->cls)) {
        /* the container allows a set of groups - postpone the
           approval check until we know this widget's group
           (if any) */
        widget->approval = widget::WIDGET_APPROVAL_UNKNOWN;
        return true;
    } else {
        /* the container does not allow any additional group,
           which means this widget's approval check has
           ultimately failed */
        widget->approval = widget::WIDGET_APPROVAL_DENIED;
        return false;
    }
}

static inline bool
widget_check_group_approval(const struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);

    if (widget->parent->cls == NULL ||
        !widget_class_has_groups(widget->parent->cls))
        return true;

    if (widget->cls == NULL)
        return false;

    return widget_class_may_embed(widget->parent->cls, widget->cls);
}

bool
widget_check_approval(struct widget *widget)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);

    if (widget->approval == widget::WIDGET_APPROVAL_UNKNOWN)
        widget->approval = widget_check_group_approval(widget)
            ? widget::WIDGET_APPROVAL_GIVEN
            : widget::WIDGET_APPROVAL_DENIED;

    return widget->approval == widget::WIDGET_APPROVAL_GIVEN;
}
