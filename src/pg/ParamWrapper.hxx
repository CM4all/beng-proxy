/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_PARAM_WRAPPER_HXX
#define PG_PARAM_WRAPPER_HXX

#include "BinaryValue.hxx"
#include "Array.hxx"

#include <iterator>
#include <cstdio>
#include <cstddef>

template<typename T, typename Enable=void>
struct PgParamWrapper {
    PgParamWrapper(const T &t);
    const char *GetValue() const;

    /**
     * Is the buffer returned by GetValue() binary?  If so, the method
     * GetSize() must return the size of the value.
     */
    bool IsBinary() const;

    /**
     * Returns the size of the value in bytes.  Only applicable if
     * IsBinary() returns true and the value is non-nullptr.
     */
    size_t GetSize() const;
};

template<>
struct PgParamWrapper<PgBinaryValue> {
    PgBinaryValue value;

    constexpr PgParamWrapper(PgBinaryValue _value)
        :value(_value) {}

    constexpr const char *GetValue() const {
        return (const char *)value.data;
    }

    static constexpr bool IsBinary() {
        return true;
    }

    constexpr size_t GetSize() const {
        return value.size;
    }
};

template<>
struct PgParamWrapper<const char *> {
    const char *value;

    constexpr PgParamWrapper(const char *_value):value(_value) {}

    constexpr const char *GetValue() const {
        return value;
    }

    static constexpr bool IsBinary() {
        return false;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<int> {
    char buffer[16];

    PgParamWrapper(int i) {
        sprintf(buffer, "%i", i);
    }

    const char *GetValue() const {
        return buffer;
    }

    static constexpr bool IsBinary() {
        return false;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<unsigned> {
    char buffer[16];

    PgParamWrapper(unsigned i) {
        sprintf(buffer, "%u", i);
    }

    const char *GetValue() const {
        return buffer;
    }

    static constexpr bool IsBinary() {
        return false;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<bool> {
    const char *value;

    constexpr PgParamWrapper(bool _value):value(_value ? "t" : "f") {}

    static constexpr bool IsBinary() {
        return false;
    }

    constexpr const char *GetValue() const {
        return value;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

/**
 * Specialization for STL container types of std::string instances.
 */
template<typename T>
struct PgParamWrapper<T,
                      std::enable_if_t<std::is_same<typename T::value_type,
                                                    std::string>::value>> {
    std::string value;

    PgParamWrapper(const T &list)
      :value(pg_encode_array(list)) {}

    static constexpr bool IsBinary() {
        return false;
    }

    const char *GetValue() const {
        return value.c_str();
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<const std::list<std::string> *> {
    std::string value;

    PgParamWrapper(const std::list<std::string> *list)
      :value(list != nullptr
             ? pg_encode_array(*list)
             : std::string()) {}

    static constexpr bool IsBinary() {
        return false;
    }

    const char *GetValue() const {
        return value.empty() ? nullptr : value.c_str();
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<typename... Params>
class PgParamCollector;

template<typename T>
class PgParamCollector<T> {
    PgParamWrapper<T> wrapper;

public:
    explicit PgParamCollector(const T &t):wrapper(t) {}

    static constexpr size_t Count() {
        return 1;
    }

    template<typename O, typename S, typename F>
    size_t Fill(O output, S size, F format) const {
        *output = wrapper.GetValue();
        *size = wrapper.GetSize();
        *format = wrapper.IsBinary();
        return 1;
    }

    template<typename O>
    size_t Fill(O output) const {
        static_assert(!decltype(wrapper)::IsBinary(),
                      "Binary values not allowed in this overload");

        *output = wrapper.GetValue();
        return 1;
    }
};

template<typename T, typename... Rest>
class PgParamCollector<T, Rest...> {
    PgParamCollector<T> first;
    PgParamCollector<Rest...> rest;

public:
    explicit PgParamCollector(const T &t, Rest... _rest)
        :first(t), rest(_rest...) {}

    static constexpr size_t Count() {
        return decltype(first)::Count() + decltype(rest)::Count();
    }

    template<typename O, typename S, typename F>
    size_t Fill(O output, S size, F format) const {
        const size_t nf = first.Fill(output, size, format);
        std::advance(output, nf);
        std::advance(size, nf);
        std::advance(format, nf);

        const size_t nr = rest.Fill(output, size, format);
        return nf + nr;
    }

    template<typename O>
    size_t Fill(O output) const {
        const size_t nf = first.Fill(output);
        std::advance(output, nf);

        const size_t nr = rest.Fill(output);
        return nf + nr;
    }
};

template<typename... Params>
class PgTextParamArray {
    PgParamCollector<Params...> collector;

public:
    static constexpr size_t count = decltype(collector)::Count();
    const char *values[count];

    explicit PgTextParamArray(Params... params):collector(params...) {
        collector.Fill(values);
    }
};

template<typename... Params>
class PgBinaryParamArray {
    PgParamCollector<Params...> collector;

public:
    static constexpr size_t count = decltype(collector)::Count();
    const char *values[count];
    int lengths[count], formats[count];

    explicit PgBinaryParamArray(Params... params):collector(params...) {
        collector.Fill(values, lengths, formats);
    }
};

#endif
