// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include <nlohmann/json_fwd.hpp>

struct AcmeDirectory;
struct AcmeAccount;
struct AcmeOrderRequest;
struct AcmeOrder;
struct AcmeChallenge;
struct AcmeAuthorization;

/**
 * Throw an exception if the given JSON document contains an "error"
 * element.
 */
void
CheckThrowError(const nlohmann::json &root);

void
from_json(const nlohmann::json &j, AcmeDirectory &directory);

[[gnu::pure]]
nlohmann::json
MakeNewAccountRequest(const char *email, bool only_return_existing) noexcept;

void
from_json(const nlohmann::json &j, AcmeAccount &account);

void
to_json(nlohmann::json &jv, const AcmeOrderRequest &request) noexcept;

void
from_json(const nlohmann::json &j, AcmeOrder &order);

void
from_json(const nlohmann::json &j, AcmeChallenge &challenge);

void
from_json(const nlohmann::json &j, AcmeAuthorization &response);
