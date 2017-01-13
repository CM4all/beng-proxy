/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_GENERAL_NAME_HXX
#define BENG_PROXY_SSL_GENERAL_NAME_HXX

#include "util/StringView.hxx"

#include <openssl/x509v3.h>

#include <utility>
#include <algorithm>

#include <assert.h>

namespace OpenSSL {

/**
 * An unmanaged GENERAL_NAME* wrapper.
 */
class GeneralName {
    GENERAL_NAME *value = nullptr;

public:
    GeneralName() = default;
    constexpr GeneralName(GENERAL_NAME *_value):value(_value) {}

    friend void swap(GeneralName &a, GeneralName &b) {
        std::swap(a.value, b.value);
    }

    constexpr operator bool() const {
        return value != nullptr;
    }

    constexpr GENERAL_NAME *get() {
        return value;
    }

    GENERAL_NAME *release() {
        return std::exchange(value, nullptr);
    }

    void clear() {
        assert(value != nullptr);

        GENERAL_NAME_free(release());
    }

    int GetType() const {
        assert(value != nullptr);

        return value->type;
    }

    StringView GetDnsName() const {
        assert(value != nullptr);
        assert(value->type == GEN_DNS);

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        const unsigned char *data = ASN1_STRING_get0_data(value->d.dNSName);
#else
        unsigned char *data = ASN1_STRING_data(value->d.dNSName);
#endif
        if (data == nullptr)
            return nullptr;

        int length = ASN1_STRING_length(value->d.dNSName);
        if (length < 0)
            return nullptr;

        return {(const char*)data, (size_t)length};
    }
};

/**
 * A managed GENERAL_NAME* wrapper.
 */
class UniqueGeneralName : public GeneralName {
public:
    UniqueGeneralName() = default;
    explicit UniqueGeneralName(GENERAL_NAME *_value):GeneralName(_value) {}

    UniqueGeneralName(GeneralName &&src)
        :GeneralName(src.release()) {}

    ~UniqueGeneralName() {
        if (*this)
            clear();
    }

    GeneralName &operator=(GeneralName &&src) {
        swap(*this, src);
        return *this;
    }
};

static inline UniqueGeneralName
ToDnsName(const char *value)
{
    return UniqueGeneralName(a2i_GENERAL_NAME(nullptr, nullptr, nullptr,
                                              GEN_DNS,
                                              const_cast<char *>(value), 0));
}

class GeneralNames {
    GENERAL_NAMES *value = nullptr;

public:
    constexpr GeneralNames(GENERAL_NAMES *_value):value(_value) {}

    friend void swap(GeneralNames &a, GeneralNames &b) {
        std::swap(a.value, b.value);
    }

    constexpr operator bool() const {
        return value != nullptr;
    }

    constexpr GENERAL_NAMES *get() {
        return value;
    }

    GENERAL_NAMES *release() {
        return std::exchange(value, nullptr);
    }

    void clear() {
        assert(value != nullptr);

        sk_GENERAL_NAME_free(release());
    }

    size_t size() const {
        assert(value != nullptr);

        return sk_GENERAL_NAME_num(value);
    }

    GeneralName operator[](size_t i) const {
        assert(value != nullptr);

        return sk_GENERAL_NAME_value(value, i);
    }

    void push_back(UniqueGeneralName &&n) {
        sk_GENERAL_NAME_push(value, n.release());
    }
};

/**
 * A managed GENERAL_NAMES* wrapper.
 */
class UniqueGeneralNames : public GeneralNames {
public:
    UniqueGeneralNames()
        :GeneralNames(sk_GENERAL_NAME_new_null()) {}

    explicit UniqueGeneralNames(GENERAL_NAMES *_value):GeneralNames(_value) {}

    UniqueGeneralNames(GeneralNames &&src)
        :GeneralNames(src.release()) {}

    ~UniqueGeneralNames() {
        if (*this)
            clear();
    }

    UniqueGeneralNames &operator=(UniqueGeneralNames &&src) {
        swap(*this, src);
        return *this;
    }
};

}

#endif
