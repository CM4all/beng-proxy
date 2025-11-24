// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Context.hxx"
#include "Widget.hxx"

WidgetContext::WidgetContext(EventLoop &_event_loop,
			     PipeStock *_pipe_stock,
			     TranslationService &_translation_service,
			     ResourceLoader &_resource_loader,
			     ResourceLoader &_filter_resource_loader,
			     WidgetRegistry *_widget_registry,
			     const char *_site_name,
			     const char *_untrusted_host,
			     const char *_local_host,
			     const char *_remote_host,
			     const char *_request_uri,
			     const char *_absolute_uri,
			     std::string_view _external_base_uri,
			     const StringMap *_args,
			     SessionManager *_session_manager,
			     const char *_session_cookie,
			     SessionId _session_id,
			     const char *_realm,
			     const StringMap *_request_headers)
	:event_loop(_event_loop),
	 pipe_stock(_pipe_stock),
	 translation_service(_translation_service),
	 resource_loader(_resource_loader),
	 filter_resource_loader(_filter_resource_loader),
	 widget_registry(_widget_registry),
	 site_name(_site_name), untrusted_host(_untrusted_host),
	 local_host(_local_host), remote_host(_remote_host),
	 uri(_request_uri), absolute_uri(_absolute_uri),
	 external_base_uri(_external_base_uri),
	 args(_args),
	 request_headers(_request_headers),
	 session_manager(_session_manager),
	 session_cookie(_session_cookie),
	 session_id(_session_id), realm(_realm) {}

WidgetContext::~WidgetContext() noexcept
{
	root_widgets.clear_and_dispose(Widget::Disposer{});
}

Widget &
WidgetContext::AddRootWidget(WidgetPtr widget) noexcept
{
	root_widgets.push_front(*widget.release());
	return root_widgets.front();
}
