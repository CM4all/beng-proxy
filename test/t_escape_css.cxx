// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "escape/CSS.hxx"
#include "escape/Class.hxx"

#include <gtest/gtest.h>

#include <string.h>

static void
check_unescape_find(const char *p, size_t offset)
{
	ASSERT_EQ(unescape_find(&css_escape_class, p), p + offset);
}

static void
check_unescape(const char *p, const char *q)
{
	static char buffer[1024];
	size_t l = unescape_buffer(&css_escape_class, p, buffer);
	ASSERT_EQ(l, strlen(q));
	ASSERT_EQ(memcmp(buffer, q, l), 0);
}

static void
check_escape_find(const char *p, size_t offset)
{
	ASSERT_EQ(escape_find(&css_escape_class, p), p + offset);
}

static void
check_escape(const char *p, const char *q)
{
	static char buffer[1024];
	size_t l = escape_buffer(&css_escape_class, p, buffer);
	ASSERT_EQ(l, strlen(q));
	ASSERT_EQ(memcmp(buffer, q, l), 0);
}

static void
check_escape_char(char p, std::string_view q)
{
	const auto result = escape_char(&css_escape_class, p);
	ASSERT_EQ(result, q);
}

TEST(CssEscape, Basic)
{
	assert(unescape_find(&css_escape_class, "foobar123") == NULL);
	check_unescape_find("\\", 0);
	check_unescape_find("foo\\\\", 3);
	check_unescape("foo\\\\", "foo\\");

	check_escape_find("foo'bar", 3);
	check_escape_find("foo\\bar", 3);
	check_escape_find("foo\"bar", 3);

	check_escape_char('\'', "\\'");
	check_escape_char('\\', "\\\\");

	check_escape("foobar", "foobar");
	check_escape("foo\\bar", "foo\\\\bar");
	check_escape("foo'bar", "foo\\'bar");
}
