// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "parser/XmlParser.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/OpenFileIstream.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "util/Exception.hxx"
#include "util/PrintException.hxx"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static bool should_exit;

class MyXmlParserHandler final : public XmlParserHandler, IstreamSink {
	XmlParser parser;

public:
	MyXmlParserHandler(struct pool &pool, UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)),
		 parser(pool, *this) {}

	void Read() noexcept {
		input.Read();
	}

	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &) noexcept override {
		return false;
	}

	bool OnXmlTagFinished(const XmlParserTag &) noexcept override {
		return true;
	}

	void OnXmlAttributeFinished(const XmlParserAttribute &) noexcept override {}

	size_t OnXmlCdata(std::string_view text, [[maybe_unused]] bool escaped,
			  [[maybe_unused]] off_t start) noexcept override {
		(void)write(STDOUT_FILENO, text.data(), text.size());
		return text.size();
	}

	/* virtual methods from class IstreamHandler */
	size_t OnData(std::span<const std::byte> src) noexcept override {
		return parser.Feed((const char *)src.data(), src.size());
	}

	void OnEof() noexcept override {
		ClearInput();
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

	auto istream = OpenFileIstream(instance.event_loop, pool,
				       "/dev/stdin");

	MyXmlParserHandler handler(pool, std::move(istream));
	while (!should_exit)
		handler.Read();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
