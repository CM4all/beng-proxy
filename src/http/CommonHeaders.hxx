// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "strmap.hxx"

/* a collection of well-known HTTP headers with hashes calculated at
   compile-time */

constexpr StringMapKey accept_encoding_header{"accept-encoding"};
constexpr StringMapKey accept_language_header{"accept-language"};
constexpr StringMapKey access_control_allow_origin_header{"access-control-allow-origin"};
constexpr StringMapKey authorization_header{"authorization"};
constexpr StringMapKey cache_control_header{"cache-control"};
constexpr StringMapKey connection_header{"connection"};
constexpr StringMapKey content_encoding_header{"content-encoding"};
constexpr StringMapKey content_length_header{"content-length"};
constexpr StringMapKey content_range_header{"content-range"};
constexpr StringMapKey content_type_header{"content-type"};
constexpr StringMapKey cookie_header{"cookie"};
constexpr StringMapKey date_header{"date"};
constexpr StringMapKey digest_header{"digest"};
constexpr StringMapKey etag_header{"etag"};
constexpr StringMapKey expect_header{"expect"};
constexpr StringMapKey expires_header{"expires"};
constexpr StringMapKey if_range_header{"if-range"};
constexpr StringMapKey if_match_header{"if-match"};
constexpr StringMapKey if_modified_since_header{"if-modified-since"};
constexpr StringMapKey if_none_match_header{"if-none-match"};
constexpr StringMapKey if_unmodified_since_header{"if-unmodified-since"};
constexpr StringMapKey host_header{"host"};
constexpr StringMapKey last_modified_header{"last-modified"};
constexpr StringMapKey location_header{"location"};
constexpr StringMapKey pragma_header{"pragma"};
constexpr StringMapKey proxy_authenticate_header{"proxy-authenticate"};
constexpr StringMapKey range_header{"range"};
constexpr StringMapKey referer_header{"referer"};
constexpr StringMapKey server_header{"server"};
constexpr StringMapKey set_cookie_header{"set-cookie"};
constexpr StringMapKey set_cookie2_header{"set-cookie2"};
constexpr StringMapKey status_header{"status"}; // for CGI
constexpr StringMapKey transfer_encoding_header{"transfer-encoding"};
constexpr StringMapKey upgrade_header{"upgrade"};
constexpr StringMapKey user_agent_header{"user-agent"};
constexpr StringMapKey vary_header{"vary"};
constexpr StringMapKey via_header{"via"};
constexpr StringMapKey x_cm4all_althost_header{"x-cm4all-althost"};
constexpr StringMapKey x_cm4all_chain_header{"x-cm4all-chain"};
constexpr StringMapKey x_cm4all_csrf_token_header{"x-cm4all-csrf-token"};
constexpr StringMapKey x_cm4all_beng_user_header{"x-cm4all-beng-user"};
constexpr StringMapKey x_cm4all_docroot_header{"x-cm4all-docroot"};
constexpr StringMapKey x_cm4all_generator_header{"x-cm4all-generator"};
constexpr StringMapKey x_cm4all_host_header{"x-cm4all-host"};
constexpr StringMapKey x_cm4all_https_header{"x-cm4all-https"};
constexpr StringMapKey x_cm4all_view_header{"x-cm4all-view"};
constexpr StringMapKey x_forwarded_for_header{"x-forwarded-for"};
