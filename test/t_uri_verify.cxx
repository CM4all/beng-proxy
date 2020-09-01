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

#include "uri/Verify.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

TEST(UriVerifyTest, Paranoid)
{
	ASSERT_TRUE(uri_path_verify_paranoid(""));
	ASSERT_TRUE(uri_path_verify_paranoid("/"));
	ASSERT_TRUE(uri_path_verify_paranoid(" "));
	ASSERT_FALSE(uri_path_verify_paranoid("."));
	ASSERT_FALSE(uri_path_verify_paranoid("./"));
	ASSERT_FALSE(uri_path_verify_paranoid("./foo"));
	ASSERT_FALSE(uri_path_verify_paranoid(".."));
	ASSERT_FALSE(uri_path_verify_paranoid("../"));
	ASSERT_FALSE(uri_path_verify_paranoid("../foo"));
	ASSERT_FALSE(uri_path_verify_paranoid(".%2e/foo"));
	ASSERT_TRUE(uri_path_verify_paranoid("foo/bar"));
	ASSERT_FALSE(uri_path_verify_paranoid("foo%2fbar"));
	ASSERT_FALSE(uri_path_verify_paranoid("/foo/bar?A%2fB%00C%"));
	ASSERT_FALSE(uri_path_verify_paranoid("foo/./bar"));
	ASSERT_TRUE(uri_path_verify_paranoid("foo//bar"));
	ASSERT_FALSE(uri_path_verify_paranoid("foo/%2ebar"));
	ASSERT_FALSE(uri_path_verify_paranoid("foo/.%2e/bar"));
	ASSERT_FALSE(uri_path_verify_paranoid("foo/.%2e"));
	ASSERT_FALSE(uri_path_verify_paranoid("foo/bar/.."));
	ASSERT_FALSE(uri_path_verify_paranoid("foo/bar/../bar"));
	ASSERT_FALSE(uri_path_verify_paranoid("f%00"));
	ASSERT_TRUE(uri_path_verify_paranoid("f%20"));
	ASSERT_TRUE(uri_path_verify_paranoid("index%2ehtml"));
}
