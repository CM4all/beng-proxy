// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * This header enables or disables certain features of the translation
 * client.  More specifically, it can be used to eliminate
 * #TranslateRequest and #TranslateResponse attributes.
 */

#pragma once

#define TRANSLATION_ENABLE_CACHE 1
#define TRANSLATION_ENABLE_WANT 1
#define TRANSLATION_ENABLE_EXPAND 1
#define TRANSLATION_ENABLE_SESSION 1
#define TRANSLATION_ENABLE_HTTP 1
#define TRANSLATION_ENABLE_WIDGET 1
#define TRANSLATION_ENABLE_RADDRESS 1
#define TRANSLATION_ENABLE_TRANSFORMATION 1
#define TRANSLATION_ENABLE_EXECUTE 1
#define TRANSLATION_ENABLE_SPAWN 1
#define TRANSLATION_ENABLE_LOGIN 0
