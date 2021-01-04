/*
 * Copyright 2007-2021 CM4all GmbH
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

#pragma once

#include "util/StringView.hxx"

#include <utility>
#include <cinttypes>

#include <stdio.h>

/**
 * A very simple JSON writing library.  Everything is written to a
 * FILE* in one single line, which makes it easy to generate JSONL
 * (JSON Lines).
 */
namespace JsonWriter {

/**
 * The sink which will receive JSON data.  This is a wrapper for a
 * FILE* which knows how to write some simple values.
 */
class Sink {
	FILE *file;

public:
	explicit Sink(FILE *_file) noexcept:file(_file) {}

	void WriteRaw(char ch) noexcept {
		fputc(ch, file);
	}

	void WriteRaw(const char *s) noexcept {
		fputs(s, file);
	}

	void NewLine() noexcept {
		WriteRaw('\n');
		fflush(file);
	}

private:
	void WriteStringChar(char ch) noexcept {
		switch (ch) {
		case '\\':
		case '"':
			WriteRaw('\\');
			WriteRaw(ch);
			break;

		case '\n':
			WriteRaw('\\');
			WriteRaw('n');
			break;

		case '\r':
			WriteRaw('\\');
			WriteRaw('r');
			break;

		default:
			if ((unsigned char)ch < 0x20)
				/* escape non-printable control characters */
				fprintf(file, "\\x%02x", ch);
			else
				WriteRaw(ch);
			break;
		}
	}

public:
	void WriteValue(StringView value) noexcept {
		WriteRaw('"');

		for (const char ch : value)
			WriteStringChar(ch);

		WriteRaw('"');
	}

	void WriteValue(const char *value) noexcept {
		WriteRaw('"');

		while (*value != 0)
			WriteStringChar(*value++);

		WriteRaw('"');
	}

	void WriteValue(std::nullptr_t) noexcept {
		WriteRaw("null");
	}

	void WriteValue(bool value) noexcept {
		WriteRaw(value ? "true" : "false");
	}

	void WriteValue(int value) noexcept {
		fprintf(file, "%i", value);
	}

	void WriteValue(unsigned value) noexcept {
		fprintf(file, "%u", value);
	}

	void WriteValue(int64_t value) noexcept {
		fprintf(file, "%" PRIi64, value);
	}

	void WriteValue(uint64_t value) noexcept {
		fprintf(file, "%" PRIu64, value);
	}

	void WriteValue(double value) noexcept {
		fprintf(file, "%f", value);
	}
};

/**
 * Write an object (dictionary, map).  Call AddMember() for each
 * member, and call Flush() once to finish this object.
 */
class Object {
	Sink sink;

	bool pending_comma = false;

public:
	explicit Object(Sink _sink) noexcept:sink(_sink) {
		sink.WriteRaw('{');
	}

	Object(const Object &) = delete;
	Object &operator=(const Object &) = delete;

	Sink AddMember(const char *name) noexcept {
		if (pending_comma) {
			sink.WriteRaw(',');
			pending_comma = false;
		}

		sink.WriteValue(name);
		sink.WriteRaw(':');
		pending_comma = true;

		return sink;
	}

	template<typename T>
	void AddMember(const char *name, T &&value) noexcept {
		AddMember(name).WriteValue(std::forward<T>(value));
	}

	void Flush() noexcept {
		sink.WriteRaw('}');
	}
};

} // namespace JsonWriter
