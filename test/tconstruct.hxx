/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "widget/View.hxx"
#include "http/Address.hxx"
#include "file_address.hxx"
#include "cgi/Address.hxx"
#include "spawn/MountList.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "AllocatorPtr.hxx"
#include "pool/pool.hxx"

#include <string.h>

struct MakeRequest : TranslateRequest {
	explicit MakeRequest(const char *_uri) {
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
	struct pool &pool;

	explicit MakeResponse(struct pool &_pool) noexcept
		:pool(_pool)
	{
		Clear();
	}

	MakeResponse(struct pool &_pool, const TranslateResponse &src)
		:pool(_pool)
	{
		FullCopyFrom(pool, src);
	}

	explicit MakeResponse(struct pool &_pool,
			      const ResourceAddress &_address,
			      const char *_base=nullptr)
		:MakeResponse(_pool)
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
		return File(*NewFromPool<FileAddress>(pool, pool, _file));
	}

	MakeResponse &&File(const char *_path,
			    const char *_base=nullptr) noexcept {
		auto f = NewFromPool<FileAddress>(pool, _path);
		f->base = _base;
		address = *f;
		return std::move(*this);
	}

	MakeResponse &&Http(const HttpAddress &_http) {
		address = _http;
		return std::move(*this);
	}

	MakeResponse &&Http(struct HttpAddress &&_http) {
		return Http(*NewFromPool<HttpAddress>(pool, pool, _http));
	}

	MakeResponse &&Cgi(const CgiAddress &_cgi) {
		address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
		return std::move(*this);
	}

	MakeResponse &&Cgi(CgiAddress &&_cgi) {
		return Cgi(*_cgi.Clone(pool));
	}

	MakeResponse &&Cgi(const char *_path, const char *_uri=nullptr,
			   const char *_path_info=nullptr) {
		auto cgi = NewFromPool<CgiAddress>(pool, _path);
		cgi->uri = _uri;
		cgi->path_info = _path_info;
		return Cgi(*cgi);
	}

	void AppendTransformation(Transformation *t) {
		if (views == nullptr) {
			views = NewFromPool<WidgetView>(pool, nullptr);
		}

		Transformation **tail = &views->transformation;
		while (*tail != nullptr)
			tail = &(*tail)->next;

		*tail = t;
	}

	MakeResponse &&Filter(const CgiAddress &_cgi) {
		auto t = NewFromPool<Transformation>(pool, FilterTransformation{});
		t->u.filter.address = ResourceAddress(ResourceAddress::Type::CGI, _cgi);
		AppendTransformation(t);
		return std::move(*this);
	}

	MakeResponse &&Filter(CgiAddress &&_cgi) {
		return Filter(*_cgi.Clone(pool));
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
	struct pool &pool;

	explicit MakeCgiAddress(struct pool &_pool,
				const char *_path, const char *_uri=nullptr,
				const char *_path_info=nullptr)
		:CgiAddress(_path), pool(_pool)
	{
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
		path_info = value;
		expand_path_info = true;
		return std::move(*this);
	}

	MakeCgiAddress &&BindMount(const char *_source, const char *_target,
				   bool _expand_source=false,
				   bool _writable=false) {
		auto *m = NewFromPool<MountList>(pool, _source, _target,
						 _expand_source, _writable);
		m->next = options.ns.mount.mounts;
		options.ns.mount.mounts = m;
		return std::move(*this);
	}
};
