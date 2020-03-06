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

#include "parser/XmlParser.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/FileIstream.hxx"
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

class MyXmlParserHandler final : public XmlParserHandler {
public:
	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(gcc_unused const XmlParserTag &tag) noexcept override {
		return false;
	}

	bool OnXmlTagFinished(const XmlParserTag &) noexcept override {
		return true;
	}

	void OnXmlAttributeFinished(gcc_unused const XmlParserAttribute &attr) noexcept override {}

	size_t OnXmlCdata(StringView text, gcc_unused bool escaped,
			  gcc_unused off_t start) noexcept override {
		(void)write(STDOUT_FILENO, text.data, text.size);
		return text.size;
	}

	void OnXmlEof(gcc_unused off_t length) noexcept override {
		should_exit = true;
	}

	void OnXmlError(std::exception_ptr ep) noexcept override {
		fprintf(stderr, "ABORT: %s\n", GetFullMessage(ep).c_str());
		exit(2);
	}
};

int
main(int argc, char **argv)
try {
	Istream *istream;

	(void)argc;
	(void)argv;

	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;

	const auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	istream = istream_file_new(instance.event_loop, pool,
				   "/dev/stdin", (off_t)-1);

	MyXmlParserHandler handler;
	auto *parser = NewFromPool<XmlParser>(pool, pool, UnusedIstreamPtr(istream), handler);

	while (!should_exit)
		parser->Read();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
