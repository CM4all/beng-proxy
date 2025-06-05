// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <utility>
#include <cinttypes>
#include <string_view>

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
				fprintf(file, "\\u%04x", ch);
			else
				WriteRaw(ch);
			break;
		}
	}

public:
	void WriteValue(std::string_view value) noexcept {
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
