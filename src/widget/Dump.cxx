/*
 * Copyright 2007-2017 Content Management AG
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

#include "Dump.hxx"
#include "Widget.hxx"
#include "istream/istream_notify.hxx"

#include <stdio.h>

static void dump_widget_tree(unsigned indent, const Widget *widget)
{
    fprintf(stderr, "%*swidget id='%s' class='%s'\n", indent, "",
            widget->id, widget->class_name);

    for (auto &child : widget->children)
        dump_widget_tree(indent + 2, &child);
}

static void
widget_dump_callback(void *ctx)
{
    const auto *widget = (const Widget *)ctx;

    dump_widget_tree(0, widget);
}

static constexpr struct istream_notify_handler widget_dump_handler = {
    .eof = widget_dump_callback,
    .abort = widget_dump_callback,
    .close = widget_dump_callback,
};

UnusedIstreamPtr
widget_dump_tree_after_istream(struct pool &pool, UnusedIstreamPtr istream,
                               Widget &widget)
{
    return istream_notify_new(pool, std::move(istream),
                              widget_dump_handler, &widget);
}
