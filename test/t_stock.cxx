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

#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "pool/pool.hxx"
#include "event/Loop.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <gtest/gtest.h>

#include <stdexcept>

#include <assert.h>

static unsigned num_create, num_fail, num_borrow, num_release, num_destroy;
static bool next_fail;
static bool got_item;
static StockItem *last_item;

struct MyStockItem final : StockItem {
	StockRequest request;

	explicit MyStockItem(CreateStockItem c)
		:StockItem(c) {}

	~MyStockItem() override {
		++num_destroy;
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		++num_borrow;
		return true;
	}

	bool Release() noexcept override {
		++num_release;
		return true;
	}
};

/*
 * stock class
 *
 */

class MyStockClass final : public StockClass {
public:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;
};

void
MyStockClass::Create(CreateStockItem c,
		     StockRequest request,
		     gcc_unused CancellablePointer &cancel_ptr)
{
	auto *item = new MyStockItem(c);
	item->request = std::move(request);

	if (next_fail) {
		++num_fail;
		delete item;
		throw std::runtime_error("next_fail");
	} else {
		++num_create;
		item->InvokeCreateSuccess();
	}
}

class MyStockGetHandler final : public StockGetHandler {
public:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override {
		assert(!got_item);

		got_item = true;
		last_item = &item;
	}

	void OnStockItemError(std::exception_ptr ep) noexcept override {
		PrintException(ep);

		got_item = true;
		last_item = nullptr;
	}
};

TEST(Stock, Basic)
{
	CancellablePointer cancel_ptr;
	StockItem *item, *second, *third;

	EventLoop event_loop;

	MyStockClass cls;
	Stock stock(event_loop, cls, "test", 3, 8,
		    Event::Duration::zero());

	MyStockGetHandler handler;

	/* create first item */

	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_TRUE(got_item);
	ASSERT_NE(last_item, nullptr);
	ASSERT_EQ(num_create, 1);
	ASSERT_EQ(num_fail, 0);
	ASSERT_EQ(num_borrow, 0);
	ASSERT_EQ(num_release, 0);
	ASSERT_EQ(num_destroy, 0);
	item = last_item;

	/* release first item */

	stock.Put(*item, false);
	event_loop.LoopNonBlock();
	ASSERT_EQ(num_create, 1);
	ASSERT_EQ(num_fail, 0);
	ASSERT_EQ(num_borrow, 0);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 0);

	/* reuse first item */

	got_item = false;
	last_item = nullptr;
	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_TRUE(got_item);
	ASSERT_EQ(last_item, item);
	ASSERT_EQ(num_create, 1);
	ASSERT_EQ(num_fail, 0);
	ASSERT_EQ(num_borrow, 1);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 0);

	/* create second item */

	got_item = false;
	last_item = nullptr;
	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_TRUE(got_item);
	ASSERT_NE(last_item, nullptr);
	ASSERT_NE(last_item, item);
	ASSERT_EQ(num_create, 2);
	ASSERT_EQ(num_fail, 0);
	ASSERT_EQ(num_borrow, 1);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 0);
	second = last_item;

	/* fail to create third item */

	next_fail = true;
	got_item = false;
	last_item = nullptr;
	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_TRUE(got_item);
	ASSERT_EQ(last_item, nullptr);
	ASSERT_EQ(num_create, 2);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 1);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 1);

	/* create third item */

	next_fail = false;
	got_item = false;
	last_item = nullptr;
	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_TRUE(got_item);
	ASSERT_NE(last_item, nullptr);
	ASSERT_EQ(num_create, 3);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 1);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 1);
	third = last_item;

	/* fourth item waiting */

	got_item = false;
	last_item = nullptr;
	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_FALSE(got_item);
	ASSERT_EQ(num_create, 3);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 1);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 1);

	/* fifth item waiting */

	stock.Get(nullptr, handler, cancel_ptr);
	ASSERT_FALSE(got_item);
	ASSERT_EQ(num_create, 3);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 1);
	ASSERT_EQ(num_release, 1);
	ASSERT_EQ(num_destroy, 1);

	/* return third item */

	stock.Put(*third, false);
	event_loop.LoopNonBlock();
	ASSERT_EQ(num_create, 3);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 2);
	ASSERT_EQ(num_release, 2);
	ASSERT_EQ(num_destroy, 1);
	ASSERT_TRUE(got_item);
	ASSERT_EQ(last_item, third);

	/* destroy second item */

	got_item = false;
	last_item = nullptr;
	stock.Put(*second, true);
	event_loop.LoopNonBlock();
	ASSERT_EQ(num_create, 4);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 2);
	ASSERT_EQ(num_release, 2);
	ASSERT_EQ(num_destroy, 2);
	ASSERT_TRUE(got_item);
	ASSERT_NE(last_item, nullptr);
	second = last_item;

	/* destroy first item */

	stock.Put(*item, true);
	ASSERT_EQ(num_create, 4);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 2);
	ASSERT_EQ(num_release, 2);
	ASSERT_EQ(num_destroy, 3);

	/* destroy second item */

	stock.Put(*second, true);
	ASSERT_EQ(num_create, 4);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 2);
	ASSERT_EQ(num_release, 2);
	ASSERT_EQ(num_destroy, 4);

	/* destroy third item */

	stock.Put(*third, true);
	ASSERT_EQ(num_create, 4);
	ASSERT_EQ(num_fail, 1);
	ASSERT_EQ(num_borrow, 2);
	ASSERT_EQ(num_release, 2);
	ASSERT_EQ(num_destroy, 5);
}
