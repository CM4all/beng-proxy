// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "translation/Request.hxx"
#include "translation/Transformation.hxx"
#include "translation/Response.hxx"
#include "translation/Layout.hxx"
#include "widget/View.hxx"
#include "http/Address.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "spawn/Mount.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

struct MakeRequest : TranslateRequest {
	TranslationLayoutItem my_layout_item;

	explicit MakeRequest(const char *_uri) {
		uri = _uri;
	}

	auto &&Layout(std::string_view value, const char *item) && noexcept {
		layout = AsBytes(value);

		if (item != nullptr) {
			my_layout_item = TranslationLayoutItem{TranslationLayoutItem::Base{}, item};
			layout_item = &my_layout_item;
		}

		return std::move(*this);
	}

	MakeRequest &&QueryString(const char *value) {
		query_string = value;
		return std::move(*this);
	}

	MakeRequest &&Check(const char *value) {
		check = {(const std::byte *)value, strlen(value)};
		return std::move(*this);
	}

	MakeRequest &&WantFullUri(std::span<const std::byte> value) {
		want_full_uri = value;
		return std::move(*this);
	}

	MakeRequest &&WantFullUri(std::string_view value) {
		return WantFullUri(AsBytes(value));
	}

	MakeRequest &&Status(HttpStatus value) noexcept {
		status = value;
		return std::move(*this);
	}
};

struct MakeResponse : TranslateResponse {
	const AllocatorPtr alloc;

	explicit MakeResponse(AllocatorPtr _alloc) noexcept
		:alloc(_alloc)
	{
		Clear();
	}

	MakeResponse(AllocatorPtr _alloc,
		     const TranslateResponse &src) noexcept
		:alloc(_alloc)
	{
		FullCopyFrom(alloc, src);
	}

	explicit MakeResponse(AllocatorPtr _alloc,
			      const ResourceAddress &_address,
			      const char *_base=nullptr) noexcept
		:MakeResponse(_alloc)
	{
		address = {ShallowCopy(), _address};
		base = _base;
	}

	MakeResponse &&Layout(std::string_view value,
			      std::initializer_list<const char *> items) && noexcept {
		layout = AsBytes(value);
		layout_items = std::make_shared<std::vector<TranslationLayoutItem>>();

		for (const char *src : items)
			layout_items->emplace_back(TranslationLayoutItem::Base{}, src);

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

	MakeResponse &&File(FileAddress &_file) {
		address = _file;
		return std::move(*this);
	}

	MakeResponse &&File(FileAddress &&_file) {
		return File(*alloc.New<FileAddress>(alloc, _file));
	}

	MakeResponse &&File(const char *_path,
			    const char *_base=nullptr) noexcept {
		auto f = alloc.New<FileAddress>(_path);
		f->base = _base;
		address = *f;
		return std::move(*this);
	}

	MakeResponse &&Http(HttpAddress &_http) {
		address = _http;
		return std::move(*this);
	}

	MakeResponse &&Http(struct HttpAddress &&_http) {
		return Http(*alloc.New<HttpAddress>(alloc, _http));
	}

	MakeResponse &&Cgi(CgiAddress &_cgi) {
		address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
		return std::move(*this);
	}

	MakeResponse &&Cgi(CgiAddress &&_cgi) {
		return Cgi(*_cgi.Clone(alloc));
	}

	MakeResponse &&Cgi(const char *_path, const char *_uri=nullptr,
			   const char *_path_info=nullptr) {
		auto cgi = alloc.New<CgiAddress>(_path);
		cgi->uri = _uri;
		cgi->path_info = _path_info;
		return Cgi(*cgi);
	}

	void AppendTransformation(Transformation *t) {
		if (views.empty()) {
			views.push_front(*alloc.New<WidgetView>(nullptr));
		}

		auto &view = views.front();
		auto i = view.transformations.before_begin();
		while (std::next(i) != view.transformations.end())
			++i;

		view.transformations.insert_after(i, *t);
	}

	MakeResponse &&Filter(CgiAddress &_cgi) {
		auto t = alloc.New<Transformation>(FilterTransformation{});
		t->u.filter.address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
		AppendTransformation(t);
		return std::move(*this);
	}

	MakeResponse &&Filter(CgiAddress &&_cgi) {
		return Filter(*_cgi.Clone(alloc));
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
		check = {(const std::byte *)value, strlen(value)};
		return std::move(*this);
	}

	MakeResponse &&WantFullUri(const char *value) {
		want_full_uri = {(const std::byte *)value, strlen(value)};
		return std::move(*this);
	}
};

struct MakeFileAddress : FileAddress {
	explicit MakeFileAddress(const char *_path):FileAddress(_path) {}

	MakeFileAddress &&ExpandPath(const char *value) {
		path = value;
		expand_path = true;
		return std::move(*this);
	}
};

struct MakeHttpAddress : HttpAddress {
	explicit MakeHttpAddress(const char *_path)
		:HttpAddress(false, "localhost:8080", _path) {}

	MakeHttpAddress &&Host(const char *value) {
		host_and_port = value;
		return std::move(*this);
	}

	MakeHttpAddress &&ExpandPath(const char *value) {
		path = value;
		expand_path = true;
		return std::move(*this);
	}
};

struct MakeCgiAddress : CgiAddress {
	const AllocatorPtr alloc;

	MakeCgiAddress(AllocatorPtr _alloc,
		       const char *_path, const char *_uri=nullptr,
		       const char *_path_info=nullptr) noexcept
		:CgiAddress(_path), alloc(_alloc)
	{
		uri = _uri;
		path_info = _path_info;
		options.no_new_privs = true;
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
		path_info = value;
		expand_path_info = true;
		return std::move(*this);
	}

	MakeCgiAddress &&BindMount(const char *_source, const char *_target,
				   bool _expand_source=false,
				   bool _writable=false) {
		auto *m = alloc.New<Mount>(_source, _target, _writable);
		m->expand_source = _expand_source;
		options.ns.mount.mounts.push_front(*m);
		return std::move(*this);
	}
};
