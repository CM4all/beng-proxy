/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_CONNECTION_HXX
#define PG_CONNECTION_HXX

#include "ParamWrapper.hxx"
#include "DynamicParamWrapper.hxx"
#include "Result.hxx"
#include "Notify.hxx"

#include <inline/compiler.h>

#include <postgresql/libpq-fe.h>

#include <new>
#include <memory>
#include <string>
#include <cassert>
#include <algorithm>
#include <stdexcept>

/**
 * A thin C++ wrapper for a PGconn pointer.
 */
class PgConnection {
    PGconn *conn = nullptr;

public:
    PgConnection() = default;

    PgConnection(const char *conninfo) {
        try {
            Connect(conninfo);
        } catch (...) {
            Disconnect();
            throw;
        }
    }

    PgConnection(const PgConnection &other) = delete;

    PgConnection(PgConnection &&other):conn(other.conn) {
        other.conn = nullptr;
    }

    PgConnection &operator=(const PgConnection &other) = delete;

    PgConnection &operator=(PgConnection &&other) {
        std::swap(conn, other.conn);
        return *this;
    }

    ~PgConnection() {
        Disconnect();
    }

    bool IsDefined() const {
        return conn != nullptr;
    }

    gcc_pure
    ConnStatusType GetStatus() const {
        assert(IsDefined());

        return ::PQstatus(conn);
    }

    gcc_pure
    const char *GetErrorMessage() const {
        assert(IsDefined());

        return ::PQerrorMessage(conn);
    }

    gcc_pure
    int GetProtocolVersion() const {
        assert(IsDefined());

        return ::PQprotocolVersion (conn);
    }

    gcc_pure
    int GetServerVersion() const {
        assert(IsDefined());

        return ::PQserverVersion (conn);
    }

    gcc_pure
    int GetBackendPID() const {
        assert(IsDefined());

        return ::PQbackendPID (conn);
    }

    gcc_pure
    int GetSocket() const {
        assert(IsDefined());

        return ::PQsocket(conn);
    }

    void Disconnect() {
        if (conn != nullptr) {
            ::PQfinish(conn);
            conn = nullptr;
        }
    }

    void Connect(const char *conninfo);

    void StartConnect(const char *conninfo) {
        assert(!IsDefined());

        conn = ::PQconnectStart(conninfo);
        if (conn == nullptr)
            throw std::bad_alloc();
    }

    PostgresPollingStatusType PollConnect() {
        assert(IsDefined());

        return ::PQconnectPoll(conn);
    }

    void Reconnect() {
        assert(IsDefined());

        ::PQreset(conn);
    }

    void StartReconnect() {
        assert(IsDefined());

        ::PQresetStart(conn);
    }

    PostgresPollingStatusType PollReconnect() {
        assert(IsDefined());

        return ::PQresetPoll(conn);
    }

    void ConsumeInput() {
        assert(IsDefined());

        ::PQconsumeInput(conn);
    }

    PgNotify GetNextNotify() {
        assert(IsDefined());

        return PgNotify(::PQnotifies(conn));
    }

protected:
    PgResult CheckResult(PGresult *result) {
        if (result == nullptr)
            throw std::bad_alloc();

        return PgResult(result);
    }

    template<size_t i, typename... Params>
    PgResult ExecuteParams3(bool result_binary,
                            const char *query,
                            const char *const*values) {
        assert(IsDefined());
        assert(query != nullptr);

        return CheckResult(::PQexecParams(conn, query, i,
                                          nullptr, values, nullptr, nullptr,
                                          result_binary));
    }

    template<size_t i, typename T, typename... Params>
    PgResult ExecuteParams3(bool result_binary,
                            const char *query, const char **values,
                            const T &t, Params... params) {
        assert(IsDefined());
        assert(query != nullptr);

        PgParamWrapper<T> p(t);
        assert(!p.IsBinary());
        values[i] = p.GetValue();

        return ExecuteParams3<i + 1, Params...>(result_binary, query,
                                                values, params...);
    }

    template<size_t i, typename... Params>
    PgResult ExecuteBinary3(const char *query,
                            const char *const*values,
                            const int *lengths, const int *formats) {
        assert(IsDefined());
        assert(query != nullptr);

        return CheckResult(::PQexecParams(conn, query, i,
                                          nullptr, values, lengths, formats,
                                          false));
    }

    template<size_t i, typename T, typename... Params>
    PgResult ExecuteBinary3(const char *query, const char **values,
                            int *lengths, int *formats,
                            const T &t, Params... params) {
        assert(IsDefined());
        assert(query != nullptr);

        PgParamWrapper<T> p(t);
        values[i] = p.GetValue();
        lengths[i] = p.GetSize();
        formats[i] = p.IsBinary();

        return ExecuteBinary3<i + 1, Params...>(query, values,
                                                lengths, formats,
                                                params...);
    }

    static size_t CountDynamic() {
        return 0;
    }

    template<typename T, typename... Params>
    static size_t CountDynamic(const T &t, Params... params) {
        return PgDynamicParamWrapper<T>::Count(t) + CountDynamic(params...);
    }

    PgResult ExecuteDynamic2(const char *query,
                             const char *const*values,
                             const int *lengths, const int *formats,
                             unsigned n) {
        assert(IsDefined());
        assert(query != nullptr);

        return CheckResult(::PQexecParams(conn, query, n,
                                          nullptr, values, lengths, formats,
                                          false));
    }

    template<typename T, typename... Params>
    PgResult ExecuteDynamic2(const char *query,
                             const char **values,
                             int *lengths, int *formats,
                             unsigned n,
                             const T &t, Params... params) {
        assert(IsDefined());
        assert(query != nullptr);

        const PgDynamicParamWrapper<T> w(t);
        n += w.Fill(values + n, lengths + n, formats + n);

        return ExecuteDynamic2(query, values, lengths, formats, n, params...);
    }

public:
    PgResult Execute(const char *query) {
        assert(IsDefined());
        assert(query != nullptr);

        return CheckResult(::PQexec(conn, query));
    }

    template<typename... Params>
    PgResult ExecuteParams(bool result_binary,
                           const char *query, Params... params) {
        assert(IsDefined());
        assert(query != nullptr);

        constexpr size_t n = sizeof...(Params);
        const char *values[n];

        return ExecuteParams3<0, Params...>(result_binary, query,
                                            values, params...);
    }

    template<typename... Params>
    PgResult ExecuteParams(const char *query, Params... params) {
        return ExecuteParams(false, query, params...);
    }

    template<typename... Params>
    PgResult ExecuteBinary(const char *query, Params... params) {
        assert(IsDefined());
        assert(query != nullptr);

        const size_t n = sizeof...(Params);
        const char *values[n];
        int lengths[n], formats[n];

        return ExecuteBinary3<0, Params...>(query, values, lengths, formats,
                                            params...);
    }

    /**
     * Execute with dynamic parameter list: this variant of
     * ExecuteParams() allows std::vector arguments which get
     * expanded.
     */
    template<typename... Params>
    PgResult ExecuteDynamic(const char *query, Params... params) {
        assert(IsDefined());
        assert(query != nullptr);

        const size_t n = CountDynamic(params...);
        std::unique_ptr<const char *[]> values(new const char *[n]);
        std::unique_ptr<int[]> lengths(new int[n]);
        std::unique_ptr<int[]> formats(new int[n]);

        return ExecuteDynamic2<Params...>(query, values.get(),
                                          lengths.get(), formats.get(), 0,
                                          params...);
    }

    bool SetSchema(const char *schema);

    bool BeginSerializable() {
        const auto result = Execute("BEGIN ISOLATION LEVEL SERIALIZABLE");
        return result.IsCommandSuccessful();
    }

    bool Commit() {
        const auto result = Execute("COMMIT");
        return result.IsCommandSuccessful();
    }

    bool Rollback() {
        const auto result = Execute("ROLLBACK");
        return result.IsCommandSuccessful();
    }

    gcc_pure
    bool IsBusy() const {
        assert(IsDefined());

        return ::PQisBusy(conn) != 0;
    }

    bool SendQuery(const char *query) {
        assert(IsDefined());
        assert(query != nullptr);

        return ::PQsendQuery(conn, query) != 0;
    }

    PgResult ReceiveResult() {
        assert(IsDefined());

        return CheckResult(::PQgetResult(conn));
    }

    gcc_pure
    std::string Escape(const char *p, size_t length) const;

    gcc_pure
    std::string Escape(const char *p) const;

    gcc_pure
    std::string Escape(const std::string &p) const {
        return Escape(p.data(), p.length());
    }
};

#endif
