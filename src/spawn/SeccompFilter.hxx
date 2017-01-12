/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SECCOMP_FILTER_HXX
#define SECCOMP_FILTER_HXX

#include "seccomp.h"

#include <stdexcept>

class SeccompFilter {
    const scmp_filter_ctx ctx;

public:
    /**
     * Throws std::runtime_error on error.
     */
    explicit SeccompFilter(uint32_t def_action);

    ~SeccompFilter() {
        seccomp_release(ctx);
    }

    SeccompFilter(const SeccompFilter &) = delete;
    SeccompFilter &operator=(const SeccompFilter &) = delete;

    /**
     * Throws std::runtime_error on error.
     */
    void Reset(uint32_t def_action);

    void Load() const;

    template<typename... Args>
    void AddRule(uint32_t action, int syscall, Args... args) {
        if (seccomp_rule_add(ctx, action, syscall, sizeof...(args),
                             std::forward<Args>(args)...) < 0)
            throw std::runtime_error("seccomp_rule_add() failed");
    }
};

#endif

