/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_GOTO_HXX
#define BENG_LB_GOTO_HXX

#include <inline/compiler.h>

class LbCluster;
class LbBranch;
class LbLuaHandler;
class LbTranslationHandler;
struct LbSimpleHttpResponse;

struct LbGoto {
    LbCluster *cluster = nullptr;
    LbBranch *branch = nullptr;
    LbLuaHandler *lua = nullptr;
    LbTranslationHandler *translation = nullptr;
    const LbSimpleHttpResponse *response = nullptr;

    LbGoto() = default;
    LbGoto(LbCluster &_cluster):cluster(&_cluster) {}
    LbGoto(LbBranch &_branch):branch(&_branch) {}
    LbGoto(LbLuaHandler &_lua):lua(&_lua) {}
    LbGoto(LbTranslationHandler &_translation):translation(&_translation) {}
    LbGoto(const LbSimpleHttpResponse &_response):response(&_response) {}

    bool IsDefined() const {
        return cluster != nullptr || branch != nullptr ||
            lua != nullptr || translation != nullptr ||
            response != nullptr;
    }

    template<typename R>
    gcc_pure
    const LbGoto &FindRequestLeaf(const R &request) const;
};

#endif
