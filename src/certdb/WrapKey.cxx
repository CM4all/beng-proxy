/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "WrapKey.hxx"

#include <openssl/err.h>

Pg::BinaryValue
UnwrapKey(Pg::BinaryValue key_der,
          const CertDatabaseConfig &config, const std::string &key_wrap_name,
          std::unique_ptr<unsigned char[]> &unwrapped)
{
    if (key_der.size <= 8)
        throw std::runtime_error("Malformed wrapped key");

    auto i = config.wrap_keys.find(key_wrap_name);
    if (i == config.wrap_keys.end())
        throw std::runtime_error(std::string("No such wrap_key: ") +
                                 key_wrap_name);

    WrapKeyHelper wrap_key_helper;
    const auto wrap_key =
        wrap_key_helper.SetDecryptKey(config, key_wrap_name);

    ERR_clear_error();

    unwrapped.reset(new unsigned char[key_der.size - 8]);
    int r = AES_unwrap_key(wrap_key, nullptr,
                           unwrapped.get(),
                           (const unsigned char *)key_der.data,
                           key_der.size);
    if (r <= 0)
        throw SslError("AES_unwrap_key() failed");

    key_der.data = unwrapped.get();
    key_der.size = r;
    return key_der;
}
