#include "translate_request.hxx"
#include "translate_response.hxx"
#include "transformation.hxx"
#include "widget_view.hxx"
#include "http_address.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "spawn/mount_list.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "pool.hxx"
#include "tpool.hxx"

#include <beng-proxy/translation.h>

#include <string.h>

struct MakeRequest : TranslateRequest {
    explicit MakeRequest(const char *_uri) {
        Clear();
        uri = _uri;
    }

    MakeRequest &QueryString(const char *value) {
        query_string = value;
        return *this;
    }

    MakeRequest &Check(const char *value) {
        check = {value, strlen(value)};
        return *this;
    }

    MakeRequest &WantFullUri(const char *value) {
        want_full_uri = {value, strlen(value)};
        return *this;
    }

    MakeRequest &WantFullUri(ConstBuffer<void> value) {
        want_full_uri = value;
        return *this;
    }

    MakeRequest &ErrorDocumentStatus(http_status_t value) {
        error_document_status = value;
        return *this;
    }
};

struct MakeResponse : TranslateResponse {
    MakeResponse() {
        Clear();
    }

    MakeResponse(const MakeResponse &src) {
        FullCopyFrom(*tpool, src);
    }

    MakeResponse(struct pool &p, const TranslateResponse &src) {
        FullCopyFrom(p, src);
    }

    explicit MakeResponse(const ResourceAddress &_address,
                          const char *_base=nullptr)
        :MakeResponse()
    {
        address = _address;
        base = _base;
    }

    MakeResponse &FullCopyFrom(struct pool &p, const TranslateResponse &src) {
        CopyFrom(&p, src);
        max_age = src.max_age;
        address.CopyFrom(p, src.address);
        user = src.user;
        return *this;
    }

    MakeResponse &Base(const char *value) {
        base = value;
        return *this;
    }

    MakeResponse &EasyBase(const char *value) {
        easy_base = true;
        return Base(value);
    }

    MakeResponse &UnsafeBase(const char *value) {
        unsafe_base = true;
        return Base(value);
    }

    MakeResponse &AutoBase() {
        auto_base = true;
        return *this;
    }

    MakeResponse &Regex(const char *value) {
        regex = value;
        return *this;
    }

    MakeResponse &RegexTail(const char *value) {
        regex_tail = true;
        return Regex(value);
    }

    MakeResponse &RegexTailUnescape(const char *value) {
        regex_unescape = true;
        return RegexTail(value);
    }

    MakeResponse &InverseRegex(const char *value) {
        inverse_regex = value;
        return *this;
    }

    MakeResponse &Uri(const char *value) {
        uri = value;
        return *this;
    }

    MakeResponse &TestPath(const char *value) {
        test_path = value;
        return *this;
    }

    MakeResponse &File(const struct file_address &_file) {
        address = ResourceAddress(_file);
        return *this;
    }

    MakeResponse &File(struct file_address &&_file) {
        return File(*file_address_dup(*tpool, &_file));
    }

    MakeResponse &File(const char *_path) {
        struct pool &p = *tpool;
        auto f = file_address_new(p, _path);
        address = ResourceAddress(*f);
        return *this;
    }

    MakeResponse &Http(const HttpAddress &_http) {
        address = ResourceAddress(ResourceAddress::Type::HTTP, _http);
        return *this;
    }

    MakeResponse &Http(struct HttpAddress &&_http) {
        return Http(*http_address_dup(*tpool, &_http));
    }

    MakeResponse &Cgi(const CgiAddress &_cgi) {
        address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
        return *this;
    }

    MakeResponse &Cgi(CgiAddress &&_cgi) {
        return Cgi(*_cgi.Clone(*tpool, false));
    }

    MakeResponse &Cgi(const char *_path, const char *_uri=nullptr,
                      const char *_path_info=nullptr) {
        struct pool &p = *tpool;
        auto cgi = cgi_address_new(p, _path);
        cgi->uri = _uri;
        cgi->path_info = _path_info;
        return Cgi(*cgi);
    }

    void AppendTransformation(Transformation *t) {
        struct pool &p = *tpool;

        if (views == nullptr) {
            views = NewFromPool<WidgetView>(p);
            views->Init(nullptr);
        }

        Transformation **tail = &views->transformation;
        while (*tail != nullptr)
            tail = &(*tail)->next;

        *tail = t;
    }

    MakeResponse &Filter(const CgiAddress &_cgi) {
        struct pool &p = *tpool;
        auto t = NewFromPool<Transformation>(p);
        t->next = nullptr;
        t->type = Transformation::Type::FILTER;
        t->u.filter.reveal_user = false;
        t->u.filter.address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
        AppendTransformation(t);
        return *this;
    }

    MakeResponse &Filter(CgiAddress &&_cgi) {
        return Filter(*_cgi.Clone(*tpool, false));
    }

    template<size_t n>
    MakeResponse &Vary(const uint16_t (&_vary)[n]) {
        vary = {_vary, n};
        return *this;
    }

    template<size_t n>
    MakeResponse &Vary(const beng_translation_command (&_vary)[n]) {
        auto data = PoolAlloc<uint16_t>(*tpool, n);
        std::copy_n(_vary, n, vary.data);
        vary = {data, n};
        return *this;
    }

    template<size_t n>
    MakeResponse &Invalidate(const uint16_t (&_invalidate)[n]) {
        invalidate = {_invalidate, n};
        return *this;
    }

    MakeResponse &Check(const char *value) {
        check = {value, strlen(value)};
        return *this;
    }

    MakeResponse &WantFullUri(const char *value) {
        want_full_uri = {value, strlen(value)};
        return *this;
    }
};

struct MakeFileAddress : file_address {
    explicit MakeFileAddress(const char *_path):file_address(_path) {}

    MakeFileAddress &ExpandPath(const char *value) {
        expand_path = value;
        return *this;
    }
};

struct MakeHttpAddress : HttpAddress {
    explicit MakeHttpAddress(const char *_path)
        :HttpAddress(URI_SCHEME_HTTP, false, "localhost:8080", _path) {}

    MakeHttpAddress &Host(const char *value) {
        host_and_port = value;
        return *this;
    }

    MakeHttpAddress &ExpandPath(const char *value) {
        expand_path = value;
        return *this;
    }
};

struct MakeCgiAddress : CgiAddress {
    explicit MakeCgiAddress(const char *_path, const char *_uri=nullptr,
                            const char *_path_info=nullptr)
        :CgiAddress(_path) {
        uri = _uri;
        path_info = _path_info;
    }

    MakeCgiAddress &ScriptName(const char *value) {
        script_name = value;
        return *this;
    }

    MakeCgiAddress &DocumentRoot(const char *value) {
        document_root = value;
        return *this;
    }

    MakeCgiAddress &ExpandPathInfo(const char *value) {
        expand_path_info = value;
        return *this;
    }

    MakeCgiAddress &BindMount(const char *_source, const char *_target,
                              bool _expand_source=false,
                              bool _writable=false) {
        auto &p = *tpool;
        auto *m = NewFromPool<MountList>(p, _source, _target,
                                         _expand_source, _writable);
        m->next = options.ns.mounts;
        options.ns.mounts = m;
        return *this;
    }
};
