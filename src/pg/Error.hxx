/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_ERROR_HXX
#define PG_ERROR_HXX

#include "Result.hxx"

#include <exception>

class PgError final : public std::exception {
    PgResult result;

public:
    PgError(const PgError &other) = delete;
    PgError(PgError &&other)
        :result(std::move(other.result)) {}
    explicit PgError(PgResult &&_result)
        :result(std::move(_result)) {}

    PgError &operator=(const PgError &other) = delete;

    PgError &operator=(PgError &&other) {
        result = std::move(other.result);
        return *this;
    }

    PgError &operator=(PgResult &&other) {
        result = std::move(other);
        return *this;
    }

    gcc_pure
    ExecStatusType GetStatus() const {
        return result.GetStatus();
    }

    gcc_pure
    const char *what() const noexcept override {
        return result.GetErrorMessage();
    }
};

#endif
