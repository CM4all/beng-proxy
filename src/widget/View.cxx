// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "View.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm> // for std::any_of()

#include <string.h>

void
WidgetView::CopyFrom(AllocatorPtr alloc, const WidgetView &src) noexcept
{
	name = alloc.CheckDup(src.name);
	address.CopyFrom(alloc, src.address);
	filter_4xx = src.filter_4xx;
	inherited = src.inherited;
	transformations = Transformation::DupChain(alloc, src.transformations);
	request_header_forward = src.request_header_forward;
	response_header_forward = src.response_header_forward;
}

WidgetView *
WidgetView::Clone(AllocatorPtr alloc) const noexcept
{
	auto dest = alloc.New<WidgetView>(nullptr);
	dest->CopyFrom(alloc, *this);
	return dest;
}

bool
WidgetView::InheritAddress(AllocatorPtr alloc,
			   const ResourceAddress &src) noexcept
{
	if (address.type != ResourceAddress::Type::NONE ||
	    src.type == ResourceAddress::Type::NONE)
		return false;

	address.CopyFrom(alloc, src);
	inherited = true;
	return true;
}

bool
WidgetView::InheritFrom(AllocatorPtr alloc, const WidgetView &src) noexcept
{
	if (InheritAddress(alloc, src.address)) {
		filter_4xx = src.filter_4xx;

		request_header_forward = src.request_header_forward;
		response_header_forward = src.response_header_forward;

		return true;
	} else
		return false;
}

bool
WidgetView::HasProcessor() const noexcept
{
	return Transformation::HasProcessor(transformations);
}

bool
WidgetView::IsContainer() const noexcept
{
	return Transformation::IsContainer(transformations);
}

bool
WidgetView::IsExpandable() const noexcept
{
	return address.IsExpandable() ||
		Transformation::IsChainExpandable(transformations);
}

void
WidgetView::Expand(AllocatorPtr alloc, const MatchData &match_data) noexcept
{
	address.Expand(alloc, match_data);
	Transformation::ExpandChain(alloc, transformations, match_data);
}
