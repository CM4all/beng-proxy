// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "translation/Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "widget/View.hxx"
#include "pool/pool.hxx"
#include "http/local/Address.hxx"
#include "http/Address.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

#include <stdio.h>

static void
print_resource_address(const ResourceAddress *address)
{
	switch (address->type) {
	case ResourceAddress::Type::NONE:
		break;

	case ResourceAddress::Type::LOCAL:
		printf("path=%s\n", address->GetFile().path);
		if (address->GetFile().content_type != nullptr)
			printf("content_type=%s\n",
			       address->GetFile().content_type);
		break;

	case ResourceAddress::Type::HTTP:
		printf("http=%s\n", address->GetHttp().path);
		break;

	case ResourceAddress::Type::LHTTP:
		printf("lhttp=%s|%s\n", address->GetLhttp().path,
		       address->GetLhttp().uri);
		break;

	case ResourceAddress::Type::PIPE:
		printf("pipe=%s\n", address->GetCgi().path);
		break;

	case ResourceAddress::Type::CGI:
		printf("cgi=%s\n", address->GetCgi().path);
		break;

	case ResourceAddress::Type::FASTCGI:
		printf("fastcgi=%s\n", address->GetCgi().path);
		break;

	case ResourceAddress::Type::WAS:
		printf("was=%s\n", address->GetCgi().path);
		break;
	}
}

class MyHandler final : public TranslateHandler {
public:
	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
MyHandler::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	const auto &response = *_response;

	if (response.status != HttpStatus{})
		printf("status=%u\n", static_cast<unsigned>(response.status));

	print_resource_address(&response.address);

	for (const auto &view : response.views) {
		if (view.name != nullptr)
			printf("view=%s\n", view.name);

		for (const auto &transformation : view.transformations) {
			switch (transformation.type) {
			case Transformation::Type::PROCESS:
				printf("process\n");
				break;

			case Transformation::Type::PROCESS_CSS:
				printf("process_css\n");
				break;

			case Transformation::Type::PROCESS_TEXT:
				printf("process_text\n");
				break;

			case Transformation::Type::FILTER:
				printf("filter\n");
				print_resource_address(&transformation.u.filter.address);
				break;
			}
		}
	}

	if (response.redirect != nullptr)
		printf("redirect=%s\n", response.redirect);
	if (response.session.data() != nullptr)
		printf("session=%.*s\n", (int)response.session.size(),
		       (const char *)response.session.data());
	if (response.user != nullptr)
		printf("user=%s\n", response.user);
}

void
MyHandler::OnTranslateError(std::exception_ptr ep) noexcept
{
	PrintException(ep);
}

int
main(int argc, char **argv)
{
	TranslateRequest request;
	request.host = "example.com";
	request.uri = "/foo/index.html";

	(void)argc;
	(void)argv;

	TestInstance instance;

	AllocatedSocketAddress translation_socket;
	translation_socket.SetLocal("@translation");

	TranslationStock stock(instance.event_loop,
			       translation_socket, 0);

	const AllocatorPtr alloc(instance.root_pool);

	MyHandler handler;
	CancellablePointer cancel_ptr;
	stock.SendRequest(alloc,
			  request, nullptr,
			  handler, cancel_ptr);

	instance.event_loop.Run();
}
