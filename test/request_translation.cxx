// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "translation/Glue.hxx"
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
#include "net/LocalSocketAddress.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

#include <fmt/core.h>

using std::string_view_literals::operator""sv;

static void
print_resource_address(const ResourceAddress *address)
{
	switch (address->type) {
	case ResourceAddress::Type::NONE:
		break;

	case ResourceAddress::Type::LOCAL:
		fmt::print("path={:?}\n", address->GetFile().path);
		if (address->GetFile().content_type != nullptr)
			fmt::print("content_type={:?}\n",
				   address->GetFile().content_type);
		break;

	case ResourceAddress::Type::HTTP:
		fmt::print("http={:?}\n", address->GetHttp().path);
		break;

	case ResourceAddress::Type::LHTTP:
		fmt::print("lhttp={:?}|{:?}\n", address->GetLhttp().path,
		       address->GetLhttp().uri);
		break;

	case ResourceAddress::Type::PIPE:
		fmt::print("pipe={:?}\n", address->GetCgi().path);
		break;

	case ResourceAddress::Type::CGI:
		fmt::print("cgi={:?}\n", address->GetCgi().path);
		break;

	case ResourceAddress::Type::FASTCGI:
		fmt::print("fastcgi={:?}\n", address->GetCgi().path);
		break;

	case ResourceAddress::Type::WAS:
		fmt::print("was={:?}\n", address->GetCgi().path);
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
		fmt::print("status=%u\n", static_cast<unsigned>(response.status));

	print_resource_address(&response.address);

	for (const auto &view : response.views) {
		if (view.name != nullptr)
			fmt::print("view={:?}\n", view.name);

		for (const auto &transformation : view.transformations) {
			switch (transformation.type) {
			case Transformation::Type::PROCESS:
				fmt::print("process\n");
				break;

			case Transformation::Type::PROCESS_CSS:
				fmt::print("process_css\n");
				break;

			case Transformation::Type::PROCESS_TEXT:
				fmt::print("process_text\n");
				break;

			case Transformation::Type::FILTER:
				fmt::print("filter\n");
				print_resource_address(&transformation.u.filter.address);
				break;
			}
		}
	}

	if (response.redirect != nullptr)
		fmt::print("redirect={:?}\n", response.redirect);
	if (response.session.data() != nullptr)
		fmt::print("session={:?}\n", ToStringView(response.session));
	if (response.user != nullptr)
		fmt::print("user={:?}\n", response.user);
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

	static constexpr LocalSocketAddress translation_socket{"@translation"sv};

	TranslationGlue stock{
		instance.event_loop,
		translation_socket,
		0,
	};

	const AllocatorPtr alloc(instance.root_pool);

	MyHandler handler;
	CancellablePointer cancel_ptr;
	stock.SendRequest(alloc,
			  request, nullptr,
			  handler, cancel_ptr);

	instance.event_loop.Run();
}
