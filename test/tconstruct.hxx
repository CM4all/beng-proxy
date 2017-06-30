#include "translation/Request.hxx"
#include "translation/Transformation.hxx"
#include "translation/Response.hxx"
#include "widget/View.hxx"
#include "http_address.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "spawn/MountList.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "AllocatorPtr.hxx"
#include "pool.hxx"
#include "tpool.hxx"

#include <string.h>

struct MakeRequest : TranslateRequest {
    explicit MakeRequest(const char *_uri) {
        Clear();
        uri = _uri;
    }

    MakeRequest &&QueryString(const char *value) {
        query_string = value;
        return std::move(*this);
    }

    MakeRequest &&Check(const char *value) {
        check = {value, strlen(value)};
        return std::move(*this);
    }

    MakeRequest &&WantFullUri(const char *value) {
        want_full_uri = {value, strlen(value)};
        return std::move(*this);
    }

    MakeRequest &&WantFullUri(ConstBuffer<void> value) {
        want_full_uri = value;
        return std::move(*this);
    }

    MakeRequest &&ErrorDocumentStatus(http_status_t value) {
        error_document_status = value;
        return std::move(*this);
    }
};

struct MakeResponse : TranslateResponse {
    MakeResponse() {
        Clear();
    }

    MakeResponse(const MakeResponse &src):TranslateResponse() {
        FullCopyFrom(*tpool, src);
    }

    MakeResponse(struct pool &p, const TranslateResponse &src) {
        FullCopyFrom(p, src);
    }

    explicit MakeResponse(const ResourceAddress &_address,
                          const char *_base=nullptr)
        :MakeResponse()
    {
        address = {ShallowCopy(), _address};
        base = _base;
    }

    MakeResponse &&FullCopyFrom(AllocatorPtr alloc,
                                const TranslateResponse &src) {
        CopyFrom(alloc, src);
        max_age = src.max_age;
        address.CopyFrom(alloc, src.address);
        user = src.user;
        return std::move(*this);
    }

    MakeResponse &&Base(const char *value) {
        base = value;
        return std::move(*this);
    }

    MakeResponse &&EasyBase(const char *value) {
        easy_base = true;
        return Base(value);
    }

    MakeResponse &&UnsafeBase(const char *value) {
        unsafe_base = true;
        return Base(value);
    }

    MakeResponse &&AutoBase() {
        auto_base = true;
        return std::move(*this);
    }

    MakeResponse &&Regex(const char *value) {
        regex = value;
        return std::move(*this);
    }

    MakeResponse &&RegexTail(const char *value) {
        regex_tail = true;
        return Regex(value);
    }

    MakeResponse &&RegexTailUnescape(const char *value) {
        regex_unescape = true;
        return RegexTail(value);
    }

    MakeResponse &&InverseRegex(const char *value) {
        inverse_regex = value;
        return std::move(*this);
    }

    MakeResponse &&Uri(const char *value) {
        uri = value;
        return std::move(*this);
    }

    MakeResponse &&Redirect(const char *value) {
        redirect = value;
        return std::move(*this);
    }

    MakeResponse &&TestPath(const char *value) {
        test_path = value;
        return std::move(*this);
    }

    MakeResponse &&File(const FileAddress &_file) {
        address = _file;
        return std::move(*this);
    }

    MakeResponse &&File(FileAddress &&_file) {
        return File(*NewFromPool<FileAddress>(*tpool, *tpool, _file));
    }

    MakeResponse &&File(const char *_path) {
        struct pool &p = *tpool;
        auto f = NewFromPool<FileAddress>(p, _path);
        address = *f;
        return std::move(*this);
    }

    MakeResponse &&Http(const HttpAddress &_http) {
        address = _http;
        return std::move(*this);
    }

    MakeResponse &&Http(struct HttpAddress &&_http) {
        return Http(*NewFromPool<HttpAddress>(*tpool, *tpool, _http));
    }

    MakeResponse &&Cgi(const CgiAddress &_cgi) {
        address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
        return std::move(*this);
    }

    MakeResponse &&Cgi(CgiAddress &&_cgi) {
        return Cgi(*_cgi.Clone(*tpool));
    }

    MakeResponse &&Cgi(const char *_path, const char *_uri=nullptr,
                       const char *_path_info=nullptr) {
        struct pool &p = *tpool;
        auto cgi = NewFromPool<CgiAddress>(p, _path);
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

    MakeResponse &&Filter(const CgiAddress &_cgi) {
        struct pool &p = *tpool;
        auto t = NewFromPool<Transformation>(p);
        t->next = nullptr;
        t->type = Transformation::Type::FILTER;
        t->u.filter.reveal_user = false;
        t->u.filter.address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
        AppendTransformation(t);
        return std::move(*this);
    }

    MakeResponse &&Filter(CgiAddress &&_cgi) {
        return Filter(*_cgi.Clone(*tpool));
    }

    template<size_t n>
    MakeResponse &&Vary(const TranslationCommand (&_vary)[n]) {
        vary = {_vary, n};
        return std::move(*this);
    }

    template<size_t n>
    MakeResponse &&Invalidate(const TranslationCommand (&_invalidate)[n]) {
        invalidate = {_invalidate, n};
        return std::move(*this);
    }

    MakeResponse &&Check(const char *value) {
        check = {value, strlen(value)};
        return std::move(*this);
    }

    MakeResponse &&WantFullUri(const char *value) {
        want_full_uri = {value, strlen(value)};
        return std::move(*this);
    }
};

struct MakeFileAddress : FileAddress {
    explicit MakeFileAddress(const char *_path):FileAddress(_path) {}

    MakeFileAddress &&ExpandPath(const char *value) {
        expand_path = value;
        return std::move(*this);
    }
};

struct MakeHttpAddress : HttpAddress {
    explicit MakeHttpAddress(const char *_path)
        :HttpAddress(HttpAddress::Protocol::HTTP, false,
                     "localhost:8080", _path) {}

    MakeHttpAddress &&Host(const char *value) {
        host_and_port = value;
        return std::move(*this);
    }

    MakeHttpAddress &&ExpandPath(const char *value) {
        expand_path = value;
        return std::move(*this);
    }
};

struct MakeCgiAddress : CgiAddress {
    explicit MakeCgiAddress(const char *_path, const char *_uri=nullptr,
                            const char *_path_info=nullptr)
        :CgiAddress(_path) {
        uri = _uri;
        path_info = _path_info;
    }

    MakeCgiAddress &&ScriptName(const char *value) {
        script_name = value;
        return std::move(*this);
    }

    MakeCgiAddress &&DocumentRoot(const char *value) {
        document_root = value;
        return std::move(*this);
    }

    MakeCgiAddress &&ExpandPathInfo(const char *value) {
        expand_path_info = value;
        return std::move(*this);
    }

    MakeCgiAddress &&BindMount(const char *_source, const char *_target,
                               bool _expand_source=false,
                               bool _writable=false) {
        auto &p = *tpool;
        auto *m = NewFromPool<MountList>(p, _source, _target,
                                         _expand_source, _writable);
        m->next = options.ns.mounts;
        options.ns.mounts = m;
        return std::move(*this);
    }
};
