// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Widget.hxx"
#include "Class.hxx"
#include "Error.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "util/StringAPI.hxx"

bool
Widget::InitApproval(bool self_container) noexcept
{
	assert(parent != nullptr);
	assert(approval == Approval::GIVEN);

	if (!self_container) {
		if (parent->cls->HasGroups())
			/* the container limits the groups; postpone a check until
			   we know the widget's group */
			approval = Approval::UNKNOWN;

		return true;
	}

	if (parent->class_name != NULL &&
	    StringIsEqual(parent->class_name, class_name))
		/* approved by SELF_CONTAINER */
		return true;

	/* failed the SELF_CONTAINER test */

	if (parent->cls->HasGroups()) {
		/* the container allows a set of groups - postpone the
		   approval check until we know this widget's group
		   (if any) */
		approval = Approval::UNKNOWN;
		return true;
	} else {
		/* the container does not allow any additional group,
		   which means this widget's approval check has
		   ultimately failed */
		approval = Approval::DENIED;
		return false;
	}
}

[[gnu::pure]]
static inline bool
widget_check_group_approval(const Widget *widget) noexcept
{
	assert(widget != NULL);
	assert(widget->parent != NULL);

	if (widget->parent->cls == nullptr || !widget->parent->cls->HasGroups())
		return true;

	if (widget->cls == NULL)
		return false;

	return widget->parent->cls->MayEmbed(*widget->cls);
}

void
Widget::CheckApproval()
{
	assert(parent != nullptr);

	if (approval == Approval::UNKNOWN)
		approval = widget_check_group_approval(this)
			? Approval::GIVEN
			: Approval::DENIED;

	if (approval != Approval::GIVEN)
		throw WidgetError(*parent, WidgetErrorCode::FORBIDDEN,
				  FmtBuffer<256>("not allowed to embed widget class '{}'",
						 class_name));
}
