// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ChildStock.hxx"
#include "ChildStockItem.hxx"
#include "net/TempListener.hxx"

#include <string_view>

/**
 * @see #ListenChildStockItem
 */
class ListenChildStockClass : public ChildStockMapClass {
public:
	virtual int GetChildSocketType(void *info) const noexcept;
	virtual unsigned GetChildBacklog(void *info) const noexcept = 0;

	/**
	 * Throws on error.
	 */
	virtual void PrepareListenChild(void *info, UniqueSocketDescriptor fd,
					PreparedChildProcess &p) = 0;

	/* virtual methods from class ChildStockMapClass */
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
