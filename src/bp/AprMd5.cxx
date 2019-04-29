/*
 * Copyright 2007-2019 Content Management AG
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

#include "AprMd5.hxx"
#include "util/StringCompare.hxx"
#include "util/StringView.hxx"

#include <openssl/md5.h>

#include <algorithm>

/**
 * A C++ wrapper for libcrypto's MD5_CTX.
 */
class CryptoMD5 {
    MD5_CTX ctx;

public:
    CryptoMD5() noexcept {
        MD5_Init(&ctx);
    }

    auto &Update(ConstBuffer<void> b) noexcept {
        MD5_Update(&ctx, b.data, b.size);
        return *this;
    }

    auto &Update(StringView s) noexcept {
        return Update(s.ToVoid());
    }

    template<typename T, size_t size>
    auto &Update(const std::array<T, size> &a) noexcept {
        return Update(ConstBuffer<T>(&a.front(), a.size()).ToVoid());
    }

    std::array<uint8_t, MD5_DIGEST_LENGTH> Final() noexcept {
        std::array<uint8_t, MD5_DIGEST_LENGTH> md;
        MD5_Final(&md.front(), &ctx);
        return md;
    }
};

static constexpr char apr1_id[] = "$apr1$";

gcc_pure
static StringView
ExtractSalt(const char *s) noexcept
{
    if (auto after_apr1_id = StringAfterPrefix(s, apr1_id))
        s = after_apr1_id;

    const char *dollar = strchr(s, '$');
    size_t length = dollar != nullptr
        ? size_t(dollar - s)
        : strlen(s);

    return {s, std::min<size_t>(length, 8)};
}

template<typename T>
static char *
To64(char *s, T v, size_t n) noexcept
{
    static constexpr char itoa64[] =
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    for (size_t i = 0; i < n; ++i) {
        *s++ = itoa64[v & 0x3f];
        v >>= 6;
    }

    return s;
}

bool
IsAprMd5(const char *crypted_password) noexcept
{
    return StringAfterPrefix(crypted_password, apr1_id) != nullptr;
}

StringBuffer<120>
AprMd5(const char *_pw, const char *_salt) noexcept
{
    const StringView pw(_pw);
    const auto salt = ExtractSalt(_salt);

    CryptoMD5 ctx;
    ctx.Update(pw);
    ctx.Update(apr1_id);
    ctx.Update(salt);

    auto f = CryptoMD5().Update(pw).Update(salt).Update(pw).Final();

    for (ssize_t i = pw.size; i > 0; i -= MD5_DIGEST_LENGTH)
        ctx.Update({&f.front(), std::min<size_t>(i, MD5_DIGEST_LENGTH)});

    for (size_t i = pw.size; i != 0; i >>= 1) {
        if (i & 1)
            ctx.Update(StringView{"\0", 1});
        else
            ctx.Update(StringView{pw.data, 1});
    }

    f = ctx.Final();

    for (unsigned i = 0; i < 1000; ++i) {
        CryptoMD5 ctx2;
        if (i & 1)
            ctx2.Update(pw);
        else
            ctx2.Update(f);

        if (i % 3)
            ctx2.Update(salt);

        if (i % 7)
            ctx2.Update(pw);

        if (i & 1)
            ctx2.Update(f);
        else
            ctx2.Update(pw);

        f = ctx2.Final();
    }

    StringBuffer<120> result;
    char *p = result.data();
    p = stpcpy(p, apr1_id);
    p = (char *)mempcpy(p, salt.data, salt.size);
    *p++ = '$';

    p = To64(p, (f[0] << 16) | (f[6] << 8) | f[12], 4);
    p = To64(p, (f[1] << 16) | (f[7] << 8) | f[13], 4);
    p = To64(p, (f[2] << 16) | (f[8] << 8) | f[14], 4);
    p = To64(p, (f[3] << 16) | (f[9] << 8) | f[15], 4);
    p = To64(p, (f[4] << 16) | (f[10] << 8) | f[ 5], 4);
    p = To64(p, f[11], 2);
    *p = '\0';

    return result;
}
