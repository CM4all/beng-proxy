/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_RESULT_HXX
#define PG_RESULT_HXX

#include "BinaryValue.hxx"

#include <inline/compiler.h>

#include <postgresql/libpq-fe.h>

#include <cassert>
#include <cstdlib>
#include <string>
#include <algorithm>

/**
 * A thin C++ wrapper for a PGresult pointer.
 */
class PgResult {
    PGresult *result;

public:
    PgResult():result(nullptr) {}
    explicit PgResult(PGresult *_result):result(_result) {}

    PgResult(const PgResult &other) = delete;
    PgResult(PgResult &&other):result(other.result) {
        other.result = nullptr;
    }

    ~PgResult() {
        if (result != nullptr)
            ::PQclear(result);
    }

    bool IsDefined() const {
        return result != nullptr;
    }

    PgResult &operator=(const PgResult &other) = delete;
    PgResult &operator=(PgResult &&other) {
        std::swap(result, other.result);
        return *this;
    }

    gcc_pure
    ExecStatusType GetStatus() const {
        assert(IsDefined());

        return ::PQresultStatus(result);
    }

    gcc_pure
    bool IsCommandSuccessful() const {
        return GetStatus() == PGRES_COMMAND_OK;
    }

    gcc_pure
    bool IsQuerySuccessful() const {
        return GetStatus() == PGRES_TUPLES_OK;
    }

    gcc_pure
    bool IsError() const {
        const auto status = GetStatus();
        return status == PGRES_BAD_RESPONSE ||
            status == PGRES_NONFATAL_ERROR ||
            status == PGRES_FATAL_ERROR;
    }

    gcc_pure
    const char *GetErrorMessage() const {
        assert(IsDefined());

        return ::PQresultErrorMessage(result);
    }

    /**
     * Returns the number of rows that were affected by the command.
     * The caller is responsible for checking GetStatus().
     */
    gcc_pure
    unsigned GetAffectedRows() const {
        assert(IsDefined());
        assert(IsCommandSuccessful());

        return std::strtoul(::PQcmdTuples(result), nullptr, 10);
    }

    /**
     * Returns true if there are no rows in the result.
     */
    gcc_pure
    bool IsEmpty() const {
        assert(IsDefined());

        return ::PQntuples(result) == 0;
    }

    gcc_pure
    unsigned GetRowCount() const {
        assert(IsDefined());

        return ::PQntuples(result);
    }

    gcc_pure
    unsigned GetColumnCount() const {
        assert(IsDefined());

        return ::PQnfields(result);
    }

    gcc_pure
    const char *GetColumnName(unsigned column) const {
        assert(IsDefined());

        return ::PQfname(result, column);
    }

    gcc_pure
    bool IsColumnBinary(unsigned column) const {
        assert(IsDefined());

        return ::PQfformat(result, column);
    }

    gcc_pure
    Oid GetColumnType(unsigned column) const {
        assert(IsDefined());

        return ::PQftype(result, column);
    }

    gcc_pure
    bool IsColumnTypeBinary(unsigned column) const {
        /* 17 = bytea */
        return GetColumnType(column) == 17;
    }

    gcc_pure
    const char *GetValue(unsigned row, unsigned column) const {
        assert(IsDefined());

        return ::PQgetvalue(result, row, column);
    }

    gcc_pure
    unsigned GetValueLength(unsigned row, unsigned column) const {
        assert(IsDefined());

        return ::PQgetlength(result, row, column);
    }

    gcc_pure
    bool IsValueNull(unsigned row, unsigned column) const {
        assert(IsDefined());

        return ::PQgetisnull(result, row, column);
    }

    gcc_pure
    PgBinaryValue GetBinaryValue(unsigned row, unsigned column) const {
        assert(IsColumnBinary(column));

        return PgBinaryValue(GetValue(row, column),
                             GetValueLength(row, column));
    }

    /**
     * Returns the only value (row 0, column 0) from the result.
     * Returns an empty string if the result is not valid or if there
     * is no row or if the value is nullptr.
     */
    gcc_pure
    std::string GetOnlyStringChecked() const;

    class RowIterator {
        PGresult *result;
        unsigned row;

    public:
        constexpr RowIterator(PGresult *_result, unsigned _row)
            :result(_result), row(_row) {}

        constexpr bool operator==(const RowIterator &other) const {
            return row == other.row;
        }

        constexpr bool operator!=(const RowIterator &other) const {
            return row != other.row;
        }

        RowIterator &operator++() {
            ++row;
            return *this;
        }

        RowIterator &operator*() {
            return *this;
        }

        gcc_pure
        const char *GetValue(unsigned column) const {
            assert(result != nullptr);
            assert(row < (unsigned)::PQntuples(result));
            assert(column < (unsigned)::PQnfields(result));

            return ::PQgetvalue(result, row, column);
        }

        gcc_pure
        unsigned GetValueLength(unsigned column) const {
            assert(result != nullptr);
            assert(row < (unsigned)::PQntuples(result));
            assert(column < (unsigned)::PQnfields(result));

            return ::PQgetlength(result, row, column);
        }

        gcc_pure
        bool IsValueNull(unsigned column) const {
            assert(result != nullptr);
            assert(row < (unsigned)::PQntuples(result));
            assert(column < (unsigned)::PQnfields(result));

            return ::PQgetisnull(result, row, column);
        }

        gcc_pure
        PgBinaryValue GetBinaryValue(unsigned column) const {
            assert(result != nullptr);
            assert(row < (unsigned)::PQntuples(result));
            assert(column < (unsigned)::PQnfields(result));

            return PgBinaryValue(GetValue(column), GetValueLength(column));
        }
    };

    typedef RowIterator iterator;

    iterator begin() const {
        return iterator{result, 0};
    }

    iterator end() const {
        return iterator{result, GetRowCount()};
    }
};

#endif
