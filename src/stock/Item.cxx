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

#include "Item.hxx"
#include "Stock.hxx"

const char *
CreateStockItem::GetStockName() const noexcept
{
	return stock.GetName();
}

void
CreateStockItem::InvokeCreateError(std::exception_ptr ep) noexcept
{
	stock.ItemCreateError(handler, ep);
}

void
CreateStockItem::InvokeCreateAborted() noexcept
{
	stock.ItemCreateAborted();
}

StockItem::~StockItem() noexcept
{
}

const char *
StockItem::GetStockName() const noexcept
{
	return stock.GetName();
}

void
StockItem::Put(bool destroy) noexcept
{
	stock.Put(*this, destroy);
}

void
StockItem::InvokeCreateSuccess() noexcept
{
	stock.ItemCreateSuccess(*this);
}

void
StockItem::InvokeCreateError(std::exception_ptr ep) noexcept
{
	stock.ItemCreateError(*this, ep);
}

void
StockItem::InvokeCreateAborted() noexcept
{
	stock.ItemCreateAborted(*this);
}

void
StockItem::InvokeIdleDisconnect() noexcept
{
	stock.ItemIdleDisconnect(*this);
}
