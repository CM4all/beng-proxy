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

#include "parser/CssParser.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "PInstance.hxx"
#include "fb_pool.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "util/Exception.hxx"
#include "util/PrintException.hxx"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static bool should_exit;

/*
 * parser handler
 *
 */

static void
my_parser_class_name(const CssParserValue *name, void *ctx) noexcept
{
	(void)ctx;

	printf(".%.*s\n", (int)name->value.size, name->value.data);
}

static void
my_parser_xml_id(const CssParserValue *id, void *ctx) noexcept
{
	(void)ctx;

	printf("#%.*s\n", (int)id->value.size, id->value.data);
}

static void
my_parser_property_keyword(const char *name, StringView value,
			   gcc_unused off_t start, gcc_unused off_t end,
			   void *ctx) noexcept
{
	(void)ctx;

	printf("%s = %.*s\n", name, int(value.size), value.data);
}

static void
my_parser_url(const CssParserValue *url, void *ctx) noexcept
{
	(void)ctx;

	printf("%.*s\n", (int)url->value.size, url->value.data);
}

static void
my_parser_import(const CssParserValue *url, void *ctx) noexcept
{
	(void)ctx;

	printf("import %.*s\n", (int)url->value.size, url->value.data);
}

static constexpr CssParserHandler my_parser_handler = {
	my_parser_class_name,
	my_parser_xml_id,
	nullptr,
	my_parser_property_keyword,
	my_parser_url,
	my_parser_import,
};

class CssParserIstreamHandler final : IstreamSink {
	CssParser parser;

public:
	explicit CssParserIstreamHandler(UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)),
		 parser(false, my_parser_handler, nullptr)
	{
	}

	void Read() noexcept {
		input.Read();
	}

	size_t OnData(const void *data, size_t length) noexcept override {
		return parser.Feed((const char *)data, length);
	}

	void OnEof() noexcept override {
		input.Clear();
		should_exit = true;
	}

	void OnError(std::exception_ptr ep) noexcept override {
		fprintf(stderr, "ABORT: %s\n", GetFullMessage(ep).c_str());
		exit(2);
	}
};

int
main(int argc, char **argv)
try {
	(void)argc;
	(void)argv;

	const ScopeFbPoolInit fb_pool_init;

	PInstance instance;
	const auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto istream = OpenFileIstream(instance.event_loop, *pool,
				       "/dev/stdin");

	CssParserIstreamHandler parser(std::move(istream));
	while (!should_exit)
		parser.Read();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
