// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Transformation.hxx"
#include "bp/XmlProcessor.hxx"
#include "AllocatorPtr.hxx"

Transformation::Transformation(AllocatorPtr alloc,
			       const Transformation &src) noexcept
	:type(src.type)
{
	switch (type) {
	case Type::PROCESS:
		new(&u.processor) XmlProcessorTransformation(src.u.processor);
		break;

	case Type::PROCESS_CSS:
		new(&u.css_processor) CssProcessorTransformation(src.u.css_processor);
		break;

	case Type::PROCESS_TEXT:
		new(&u.text_processor) TextProcessorTransformation(src.u.text_processor);
		break;

	case Type::FILTER:
		new(&u.filter) FilterTransformation(alloc, src.u.filter);
		break;

	case Type::SUBST:
		new(&u.subst) SubstTransformation(alloc, src.u.subst);
		break;
	}
}

bool
Transformation::HasProcessor(const IntrusiveForwardList<Transformation> &list) noexcept
{
	for (const auto &i : list)
		if (i.type == Type::PROCESS)
			return true;

	return false;
}

bool
Transformation::IsContainer(const IntrusiveForwardList<Transformation> &list) noexcept
{
	for (const auto &i : list)
		if (i.type == Type::PROCESS)
			return (i.u.processor.options & PROCESSOR_CONTAINER) != 0;

	return false;
}

Transformation *
Transformation::Dup(AllocatorPtr alloc) const noexcept
{
	return alloc.New<Transformation>(alloc, *this);
}

IntrusiveForwardList<Transformation>
Transformation::DupChain(AllocatorPtr alloc,
			 const IntrusiveForwardList<Transformation> &src) noexcept
{
	IntrusiveForwardList<Transformation> dest;
	auto tail = dest.before_begin();

	for (const auto &i : src)
		tail = dest.insert_after(tail, *i.Dup(alloc));

	return dest;
}

bool
Transformation::IsChainExpandable(const IntrusiveForwardList<Transformation> &list) noexcept
{
	for (const auto &i : list)
		if (i.IsExpandable())
			return true;

	return false;
}

void
Transformation::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	switch (type) {
	case Type::PROCESS:
	case Type::PROCESS_CSS:
	case Type::PROCESS_TEXT:
		break;

	case Type::FILTER:
		u.filter.Expand(alloc, match_data);
		break;

	case Type::SUBST:
		break;
	}
}

void
Transformation::ExpandChain(AllocatorPtr alloc,
			    IntrusiveForwardList<Transformation> &list,
			    const MatchData &match_data)
{
	for (auto &i : list)
		i.Expand(alloc, match_data);
}
