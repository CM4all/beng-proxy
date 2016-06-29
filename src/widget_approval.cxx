/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_approval.hxx"
#include "widget_class.hxx"
#include "widget.hxx"

bool
widget_init_approval(Widget *widget, bool self_container)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);
    assert(widget->approval == Widget::Approval::GIVEN);

    const Widget *parent = widget->parent;

    if (!self_container) {
        if (widget_class_has_groups(parent->cls))
            /* the container limits the groups; postpone a check until
               we know the widget's group */
            widget->approval = Widget::Approval::UNKNOWN;

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
        widget->approval = Widget::Approval::UNKNOWN;
        return true;
    } else {
        /* the container does not allow any additional group,
           which means this widget's approval check has
           ultimately failed */
        widget->approval = Widget::Approval::DENIED;
        return false;
    }
}

static inline bool
widget_check_group_approval(const Widget *widget)
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
widget_check_approval(Widget *widget)
{
    assert(widget != NULL);
    assert(widget->parent != NULL);

    if (widget->approval == Widget::Approval::UNKNOWN)
        widget->approval = widget_check_group_approval(widget)
            ? Widget::Approval::GIVEN
            : Widget::Approval::DENIED;

    return widget->approval == Widget::Approval::GIVEN;
}
