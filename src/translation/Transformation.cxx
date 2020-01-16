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

#include "Transformation.hxx"
#include "bp/XmlProcessor.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

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
Transformation::HasProcessor(const Transformation *t) noexcept
{
	for (; t != nullptr; t = t->next)
		if (t->type == Type::PROCESS)
			return true;

	return false;
}

bool
Transformation::IsContainer(const Transformation *t) noexcept
{
	for (; t != nullptr; t = t->next)
		if (t->type == Type::PROCESS)
			return (t->u.processor.options & PROCESSOR_CONTAINER) != 0;

	return false;
}

Transformation *
Transformation::Dup(AllocatorPtr alloc) const noexcept
{
	return alloc.New<Transformation>(alloc, *this);
}

Transformation *
Transformation::DupChain(AllocatorPtr alloc,
			 const Transformation *src) noexcept
{
	Transformation *dest = nullptr, **tail_p = &dest;

	for (; src != nullptr; src = src->next) {
		Transformation *p = src->Dup(alloc);
		*tail_p = p;
		tail_p = &p->next;
	}

	return dest;
}

bool
Transformation::IsChainExpandable() const noexcept
{
	for (auto t = this; t != nullptr; t = t->next)
		if (t->IsExpandable())
			return true;

	return false;
}

void
Transformation::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
	switch (type) {
	case Type::PROCESS:
	case Type::PROCESS_CSS:
	case Type::PROCESS_TEXT:
		break;

	case Type::FILTER:
		u.filter.Expand(alloc, match_info);
		break;

	case Type::SUBST:
		break;
	}
}

void
Transformation::ExpandChain(AllocatorPtr alloc, const MatchInfo &match_info)
{
	for (auto t = this; t != nullptr; t = t->next)
		t->Expand(alloc, match_info);
}
