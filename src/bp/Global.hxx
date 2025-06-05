// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Global variables which are not worth passing around.
 */

#pragma once

class TranslationService;
class PipeStock;

extern TranslationService *global_translation_service;

extern PipeStock *global_pipe_stock;
