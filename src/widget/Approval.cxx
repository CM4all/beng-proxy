/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Widget.hxx"
#include "Class.hxx"
#include "Error.hxx"
#include "util/StringFormat.hxx"

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
	    strcmp(parent->class_name, class_name) == 0)
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

gcc_pure
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
				  StringFormat<256>("not allowed to embed widget class '%s'",
						    class_name));
}
