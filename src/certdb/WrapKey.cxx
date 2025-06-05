// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "WrapKey.hxx"
#include "Config.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/UniqueEVP.hxx"
#include "lib/sodium/SecretBox.hxx"
#include "util/AllocatedArray.hxx"

#include <openssl/err.h>

#include <sodium/randombytes.h>

#include <algorithm>
#include <stdexcept>

AllocatedArray<std::byte>
WrapKey::EncryptAES256(std::span<const std::byte> src) const
{
	AllocatedArray<std::byte> padded;
	size_t padded_size = ((src.size() - 1) | 7) + 1;
	if (padded_size != src.size()) {
		/* pad with zeroes */
		padded.ResizeDiscard(padded_size);

		auto p = padded.begin();
		p = std::copy(src.begin(), src.end(), p);
		std::fill(p, padded.end(), std::byte{});

		src = padded;
	}

	AllocatedArray<std::byte> dest{src.size() + 8};

	ERR_clear_error();

	UniqueEVP_CIPHER_CTX ctx{EVP_CIPHER_CTX_new()};
	if (!ctx)
		throw SslError{"EVP_CIPHER_CTX_new() failed"};

	EVP_CIPHER_CTX_set_flags(ctx.get(), EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);

	if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_wrap(), nullptr,
			       reinterpret_cast<const unsigned char *>(key.data()),
			       nullptr) != 1)
		throw SslError{"EVP_EncryptInit_ex() failed"};

	int outlen;
	if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(dest.data()), &outlen,
			      reinterpret_cast<const unsigned char *>(src.data()), src.size()) != 1)
		throw SslError{"EVP_EncryptUpdate() failed"};

	std::size_t dest_position = outlen;

	if (EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(dest.data() + dest_position), &outlen) != 1)
		throw SslError{"EVP_EncryptFinal_ex() failed"};

	dest_position += outlen;
	assert(dest_position <= dest.size());
	dest.SetSize(dest_position);
	return dest;
}

AllocatedArray<std::byte>
WrapKey::DecryptAES256(std::span<const std::byte> src) const
{
	if (src.size() <= 8)
		throw std::runtime_error("Malformed wrapped key");

	AllocatedArray<std::byte> dest{src.size() - 8};

	ERR_clear_error();

	UniqueEVP_CIPHER_CTX ctx{EVP_CIPHER_CTX_new()};
	if (!ctx)
		throw SslError{"EVP_CIPHER_CTX_new() failed"};

	EVP_CIPHER_CTX_set_flags(ctx.get(), EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);

	if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_wrap(), nullptr,
			       reinterpret_cast<const unsigned char *>(key.data()),
			       nullptr) != 1)
		throw SslError{"EVP_DecryptInit_ex() failed"};

	int outlen;
	if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(dest.data()), &outlen,
			      reinterpret_cast<const unsigned char *>(src.data()), src.size()) != 1)
		throw SslError{"EVP_DecryptUpdate() failed"};

	std::size_t dest_position = outlen;

	if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(dest.data() + dest_position), &outlen) != 1)
		throw SslError{"EVP_DecryptFinal_ex() failed"};

	dest_position += outlen;
	assert(dest_position <= dest.size());
	dest.SetSize(dest_position);
	return dest;
}

static AllocatedArray<std::byte>
EncryptSecretBox(WrapKeyView key, std::span<const std::byte> src)
{
	AllocatedArray<std::byte> result{crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + src.size()};
	const std::span<std::byte> dest = result;

	/* generate a random nonce and prepend it in the result */
	const auto nonce = dest.first<crypto_secretbox_NONCEBYTES>();
	randombytes_buf(reinterpret_cast<unsigned char *>(nonce.data()), nonce.size());

	/* write the encrypted data after the nonce */
	crypto_secretbox_easy(dest.data() + crypto_secretbox_NONCEBYTES,
			      src, nonce, key);

	return result;
}

static AllocatedArray<std::byte>
DecryptSecretBox(WrapKeyView key, std::span<const std::byte> src)
{
	if (src.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
		throw std::runtime_error("Malformed wrapped key");

	AllocatedArray<std::byte> result{src.size() - crypto_secretbox_NONCEBYTES - crypto_secretbox_MACBYTES};

	const auto nonce = src.first<crypto_secretbox_NONCEBYTES>();
	src = src.subspan(crypto_secretbox_NONCEBYTES);

	if (!crypto_secretbox_open_easy(result.data(), src, nonce, key))
		throw std::invalid_argument{"Failed to decrypt key"};

	return result;
}

AllocatedArray<std::byte>
WrapKey::Encrypt(std::span<const std::byte> src) const
{
	/* encrypt with SecretBox by default */
	return EncryptSecretBox(key, src);
}

AllocatedArray<std::byte>
WrapKey::Decrypt(std::span<const std::byte> src) const
{
	try {
		/* try SecretBox first */
		return DecryptSecretBox(key, src);
	} catch (...) {
		/* if that fails, fall back to AES256 (legacu) */
		// TODO remove AES256 support when all databases have been converted
		try {
			return DecryptAES256(src);
		} catch (...) {
		}

		throw;
	}
}
