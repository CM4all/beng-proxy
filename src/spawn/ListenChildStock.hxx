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

#pragma once

#include "ChildStock.hxx"
#include "ChildStockItem.hxx"
#include "net/TempListener.hxx"

#include <string_view>

/**
 * @see #ListenChildStockItem
 */
class ListenChildStockClass : public ChildStockClass {
public:
	virtual int GetChildSocketType(void *info) const noexcept;
	virtual unsigned GetChildBacklog(void *info) const noexcept = 0;

	/**
	 * Throws on error.
	 */
	virtual void PrepareListenChild(void *info, UniqueSocketDescriptor fd,
					PreparedChildProcess &p) = 0;

	/* virtual methods from class ChildStockClass */
	std::unique_ptr<ChildStockItem> CreateChild(CreateStockItem c,
						    void *info,
						    ChildStock &child_stock) override;
};

/**
 * A #ChildStockItem implementation which passes a (private) listener
 * socket to the child process.
 */
class ListenChildStockItem : public ChildStockItem
{
	TempListener socket;

public:
	ListenChildStockItem(CreateStockItem c,
			     ChildStock &_child_stock,
			     std::string_view _tag) noexcept
		:ChildStockItem(c, _child_stock, _tag) {}

	/**
	 * Connect a socket to the given child process.  The socket
	 * must be closed before the #StockItem is returned.
	 *
	 * Throws on error.
	 *
	 * @return a socket
	 */
	UniqueSocketDescriptor Connect();

protected:
	/* virtual methods from class ChildStockItem */
	void Prepare(ChildStockClass &cls, void *info,
		     PreparedChildProcess &p) override;
};
