/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "translation/Request.hxx"
#include "translation/Transformation.hxx"
#include "translation/Response.hxx"
#include "translation/Layout.hxx"
#include "widget/View.hxx"
#include "http/Address.hxx"
#include "file_address.hxx"
#include "cgi/Address.hxx"
#include "spawn/Mount.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

struct MakeRequest : TranslateRequest {
	TranslationLayoutItem my_layout_item;

	explicit MakeRequest(const char *_uri) {
		uri = _uri;
	}

	auto &&Layout(StringView value, const char *item) && noexcept {
		layout = value.ToVoid();

		if (item != nullptr) {
			my_layout_item = TranslationLayoutItem{item};
			layout_item = &my_layout_item;
		}

		return std::move(*this);
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

	MakeRequest &&Status(http_status_t value) noexcept {
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
		FullCopyFrom(src);
	}

	explicit MakeResponse(AllocatorPtr _alloc,
			      const ResourceAddress &_address,
			      const char *_base=nullptr) noexcept
		:MakeResponse(_alloc)
	{
		address = {ShallowCopy(), _address};
		base = _base;
	}

	MakeResponse &&FullCopyFrom(const TranslateResponse &src) noexcept {
		CopyFrom(alloc, src);
		max_age = src.max_age;
		address.CopyFrom(alloc, src.address);
		user = src.user;
		return std::move(*this);
	}

	MakeResponse &&Layout(StringView value,
			      std::initializer_list<const char *> items) && noexcept {
		layout = value.ToVoid();
		layout_items = alloc.ConstructArray<TranslationLayoutItem>(items);
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
		return File(*alloc.New<FileAddress>(alloc, _file));
	}

	MakeResponse &&File(const char *_path,
			    const char *_base=nullptr) noexcept {
		auto f = alloc.New<FileAddress>(_path);
		f->base = _base;
		address = *f;
		return std::move(*this);
	}

	MakeResponse &&Http(const HttpAddress &_http) {
		address = _http;
		return std::move(*this);
	}

	MakeResponse &&Http(struct HttpAddress &&_http) {
		return Http(*alloc.New<HttpAddress>(alloc, _http));
	}

	MakeResponse &&Cgi(const CgiAddress &_cgi) {
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
		if (views == nullptr) {
			views = alloc.New<WidgetView>(nullptr);
		}

		auto i = views->transformations.before_begin();
		while (std::next(i) != views->transformations.end())
			++i;

		views->transformations.insert_after(i, *t);
	}

	MakeResponse &&Filter(const CgiAddress &_cgi) {
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
