/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_GOTO_CONFIG_HXX
#define BENG_LB_GOTO_CONFIG_HXX

#include "regex.hxx"
#include "ClusterConfig.hxx"
#include "SimpleHttpResponse.hxx"

#include <inline/compiler.h>

#include <boost/filesystem/path.hpp>

#include <string>
#include <list>

struct LbAttributeReference {
    enum class Type {
        METHOD,
        URI,
        HEADER,
    } type;

    std::string name;

    LbAttributeReference(Type _type)
        :type(_type) {}

    template<typename N>
    LbAttributeReference(Type _type, N &&_name)
        :type(_type), name(std::forward<N>(_name)) {}

    template<typename R>
    gcc_pure
    const char *GetRequestAttribute(const R &request) const {
        switch (type) {
        case Type::METHOD:
            return http_method_to_string(request.method);

        case Type::URI:
            return request.uri;

        case Type::HEADER:
            return request.headers.Get(name.c_str());
        }

        assert(false);
        gcc_unreachable();
    }

};

struct LbBranchConfig;
struct LbLuaHandlerConfig;

struct LbGoto {
    const LbClusterConfig *cluster = nullptr;
    const LbBranchConfig *branch = nullptr;
    const LbLuaHandlerConfig *lua = nullptr;
    LbSimpleHttpResponse response;

    LbGoto() = default;

    explicit LbGoto(LbClusterConfig *_cluster)
        :cluster(_cluster) {}

    explicit LbGoto(LbBranchConfig *_branch)
        :branch(_branch) {}

    explicit LbGoto(LbLuaHandlerConfig *_lua)
        :lua(_lua) {}

    explicit LbGoto(http_status_t _status)
        :response(_status) {}

    bool IsDefined() const {
        return cluster != nullptr || branch != nullptr ||
            lua != nullptr ||
            response.IsDefined();
    }

    gcc_pure
    LbProtocol GetProtocol() const;

    gcc_pure
    const char *GetName() const;

    bool HasZeroConf() const;

    template<typename R>
    gcc_pure
    const LbGoto &FindRequestLeaf(const R &request) const;
};

struct LbConditionConfig {
    LbAttributeReference attribute_reference;

    enum class Operator {
        EQUALS,
        REGEX,
    };

    Operator op;

    bool negate;

    std::string string;
    UniqueRegex regex;

    LbConditionConfig(LbAttributeReference &&a, bool _negate,
                      const char *_string)
        :attribute_reference(std::move(a)), op(Operator::EQUALS),
         negate(_negate), string(_string) {}

    LbConditionConfig(LbAttributeReference &&a, bool _negate,
                      UniqueRegex &&_regex)
        :attribute_reference(std::move(a)), op(Operator::REGEX),
         negate(_negate), regex(std::move(_regex)) {}

    LbConditionConfig(LbConditionConfig &&other) = default;

    LbConditionConfig(const LbConditionConfig &) = delete;
    LbConditionConfig &operator=(const LbConditionConfig &) = delete;

    gcc_pure
    bool Match(const char *value) const {
        switch (op) {
        case Operator::EQUALS:
            return (string == value) ^ negate;

        case Operator::REGEX:
            return regex.Match(value) ^ negate;
        }

        gcc_unreachable();
    }

    template<typename R>
    gcc_pure
    bool MatchRequest(const R &request) const {
        const char *value = attribute_reference.GetRequestAttribute(request);
        if (value == nullptr)
            value = "";

        return Match(value);
    }
};

struct LbGotoIfConfig {
    LbConditionConfig condition;

    LbGoto destination;

    LbGotoIfConfig(LbConditionConfig &&c, LbGoto d)
        :condition(std::move(c)), destination(d) {}

    bool HasZeroConf() const {
        return destination.HasZeroConf();
    }
};

/**
 * An object that distributes connections or requests to the "real"
 * cluster.
 */
struct LbBranchConfig {
    std::string name;

    LbGoto fallback;

    std::list<LbGotoIfConfig> conditions;

    explicit LbBranchConfig(const char *_name)
        :name(_name) {}

    LbBranchConfig(LbBranchConfig &&) = default;

    LbBranchConfig(const LbBranchConfig &) = delete;
    LbBranchConfig &operator=(const LbBranchConfig &) = delete;

    bool HasFallback() const {
        return fallback.IsDefined();
    }

    LbProtocol GetProtocol() const {
        return fallback.GetProtocol();
    }

    bool HasZeroConf() const {
        if (fallback.HasZeroConf())
            return true;

        for (const auto &i : conditions)
            if (i.HasZeroConf())
                return true;

        return false;
    }

    template<typename R>
    gcc_pure
    const LbGoto &FindRequestLeaf(const R &request) const {
        for (const auto &i : conditions)
            if (i.condition.MatchRequest(request))
                return i.destination.FindRequestLeaf(request);

        return fallback.FindRequestLeaf(request);
    }
};

/**
 * An HTTP request handler implemented in Lua.
 */
struct LbLuaHandlerConfig {
    std::string name;

    boost::filesystem::path path;
    std::string function;

    explicit LbLuaHandlerConfig(const char *_name)
        :name(_name) {}

    LbLuaHandlerConfig(LbLuaHandlerConfig &&) = default;

    LbLuaHandlerConfig(const LbLuaHandlerConfig &) = delete;
    LbLuaHandlerConfig &operator=(const LbLuaHandlerConfig &) = delete;
};

inline LbProtocol
LbGoto::GetProtocol() const
{
    assert(IsDefined());

    if (response.IsDefined() || lua != nullptr)
        return LbProtocol::HTTP;

    return cluster != nullptr
        ? cluster->protocol
        : branch->GetProtocol();
}

inline const char *
LbGoto::GetName() const
{
    assert(IsDefined());

    if (lua != nullptr)
        return lua->name.c_str();

    return cluster != nullptr
        ? cluster->name.c_str()
        : branch->name.c_str();
}

inline bool
LbGoto::HasZeroConf() const
{
    return (cluster != nullptr && cluster->HasZeroConf()) ||
        (branch != nullptr && branch->HasZeroConf());
}

template<typename R>
const LbGoto &
LbGoto::FindRequestLeaf(const R &request) const
{
    if (branch != nullptr)
        return branch->FindRequestLeaf(request);

    return *this;
}

#endif
