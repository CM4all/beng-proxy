// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "parser/CssParser.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
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

	printf(".%.*s\n", (int)name->value.size(), name->value.data());
}

static void
my_parser_xml_id(const CssParserValue *id, void *ctx) noexcept
{
	(void)ctx;

	printf("#%.*s\n", (int)id->value.size(), id->value.data());
}

static void
my_parser_property_keyword(const char *name, std::string_view value,
			   [[maybe_unused]] off_t start,
			   [[maybe_unused]] off_t end,
			   void *ctx) noexcept
{
	(void)ctx;

	printf("%s = %.*s\n", name, int(value.size()), value.data());
}

static void
my_parser_url(const CssParserValue *url, void *ctx) noexcept
{
	(void)ctx;

	printf("%.*s\n", (int)url->value.size(), url->value.data());
}

static void
my_parser_import(const CssParserValue *url, void *ctx) noexcept
{
	(void)ctx;

	printf("import %.*s\n", (int)url->value.size(), url->value.data());
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

	size_t OnData(std::span<const std::byte> src) noexcept override {
		return parser.Feed((const char *)src.data(), src.size());
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

	TestInstance instance;
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
