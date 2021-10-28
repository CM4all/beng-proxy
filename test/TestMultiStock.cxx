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

#include "stock/MultiStock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Class.hxx"
#include "event/Loop.hxx"

#include <gtest/gtest.h>

#include <list>

struct Partition;

struct MyStockItem final : StockItem {
	const StockRequest request;

	explicit MyStockItem(CreateStockItem c, StockRequest _request) noexcept
		:StockItem(c), request(std::move(_request)) {}

	~MyStockItem() noexcept;

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		return true;
	}

	bool Release() noexcept override {
		return true;
	}
};

struct MyInnerStockItem final : StockItem {
	StockItem &outer_item;

	MyInnerStockItem(CreateStockItem c, StockItem &_outer_item) noexcept
		:StockItem(c), outer_item(_outer_item) {}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		return true;
	}

	bool Release() noexcept override {
		return true;
	}
};

class MyStockClass final : public StockClass, public MultiStockClass {
	class DeferredRequest final : Cancellable {
		Partition &partition;

		CreateStockItem c;
		StockRequest request;

		DeferEvent defer_event;

	public:
		DeferredRequest(Partition &_partition,
				CreateStockItem _c, StockRequest _request,
				CancellablePointer &cancel_ptr) noexcept;

	private:
		void OnDeferred() noexcept;
		void Cancel() noexcept override;
	};

public:
	/* virtual methods from class StockClass */
	Event::Duration GetClearInterval(void *) const noexcept override {
		return std::chrono::hours{1};
	}

	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;

	/* virtual methods from class MultiStockClass */
	StockItem *Create(CreateStockItem c, StockItem &outer_item) override {
		return new MyInnerStockItem(c, outer_item);
	}
};

struct Instance {
	EventLoop event_loop;

	MyStockClass stock_class;
	MultiStock multi_stock;

	explicit Instance(unsigned limit=1) noexcept
		:multi_stock(event_loop, stock_class, limit, limit,
			     stock_class) {}

	void RunSome() noexcept {
		for (unsigned i = 0; i < 8; ++i)
			event_loop.LoopOnceNonBlock();
	}
};

struct MyLease;

struct Partition {
	Instance &instance;
	const char *const key;

	std::size_t factory_created = 0, factory_failed = 0;
	std::size_t destroyed = 0;
	std::size_t total = 0, waiting = 0, ready = 0, failed = 0;

	std::list<MyLease> leases;

	/**
	 * This error will be produced by MyStockClass::Create().
	 */
	std::exception_ptr next_error;

	bool defer_create = false;

	Partition(Instance &_instance, const char *_key) noexcept
		:instance(_instance), key(_key) {}

	MyLease &Get() noexcept;

	void Get(std::size_t n) noexcept {
		for (std::size_t i = 0; i < n; ++i)
			Get();
	}

	void PutReady(unsigned n=256) noexcept;
	void PutDirty(unsigned n) noexcept;
	void PutOuterDirty() noexcept;
};

struct MyLease final : public StockGetHandler {
	Partition &partition;

	CancellablePointer get_cancel_ptr;

	MyInnerStockItem *item = nullptr;
	std::exception_ptr error;

	bool reuse = true;

	explicit MyLease(Partition &_partition) noexcept
		:partition(_partition)
	{
		++partition.total;
		++partition.waiting;
	}

	~MyLease() noexcept {
		assert(partition.total > 0);

		if (get_cancel_ptr) {
			assert(partition.waiting > 0);
			--partition.waiting;

			get_cancel_ptr.Cancel();
		} else if (item != nullptr)
			Release();

		--partition.total;
	}

	MyLease(const MyLease &) = delete;
	MyLease &operator=(const MyLease &) = delete;

	void SetDirty() noexcept {
		reuse = false;
	}

	void Release() noexcept {
		assert(item != nullptr);
		assert(partition.total > 0);
		assert(partition.ready > 0);

		--partition.ready;

		if (!reuse)
			item->outer_item.fade = true;
		item->Put(!reuse);
		item = nullptr;
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &_item) noexcept override {
		assert(item == nullptr);
		assert(!error);
		assert(partition.total > 0);
		assert(partition.waiting > 0);

		get_cancel_ptr = nullptr;
		item = (MyInnerStockItem *)&_item;
		++partition.ready;
		--partition.waiting;
	}

	void OnStockItemError(std::exception_ptr _error) noexcept override {
		assert(item == nullptr);
		assert(!error);
		assert(partition.total > 0);
		assert(partition.waiting > 0);

		get_cancel_ptr = nullptr;
		error = std::move(_error);
		--partition.waiting;
		++partition.failed;
	}
};

MyLease &
Partition::Get() noexcept
{
	leases.emplace_back(*this);
	auto &lease = leases.back();
	instance.multi_stock.Get(key, ToNopPointer(this), 2,
				 lease, lease.get_cancel_ptr);
	return lease;
}

void
Partition::PutReady(unsigned n) noexcept
{
	for (auto i = leases.begin(), end = leases.end(); n > 0 && i != end;) {
		if (i->item != nullptr) {
			i = leases.erase(i);
			--n;
		} else
			++i;
	}
}

void
Partition::PutDirty(unsigned n) noexcept
{
	for (auto i = leases.begin(); n > 0;) {
		assert(i != leases.end());

		if (i->item != nullptr) {
			i->SetDirty();
			i = leases.erase(i);
			--n;
		} else
			++i;
	}

	// unreachable
}

void
Partition::PutOuterDirty() noexcept
{
	assert(!leases.empty());

	auto &item = leases.front();
	auto &outer_item = item.item->outer_item;
	leases.pop_front();

	outer_item.InvokeBusyDisconnect();
}

MyStockClass::DeferredRequest::DeferredRequest(Partition &_partition,
					       CreateStockItem _c,
					       StockRequest _request,
					       CancellablePointer &cancel_ptr) noexcept
	:partition(_partition), c(_c), request(std::move(_request)),
	 defer_event(partition.instance.event_loop, BIND_THIS_METHOD(OnDeferred))
{
	cancel_ptr = *this;
	defer_event.Schedule();
}

void
MyStockClass::DeferredRequest::OnDeferred() noexcept
{
	if (partition.next_error) {
		++partition.factory_failed;

		c.InvokeCreateError(partition.next_error);
	} else {
		++partition.factory_created;

		auto *item = new MyStockItem(c, std::move(request));
		item->InvokeCreateSuccess();
	}

	delete this;
}

void
MyStockClass::DeferredRequest::Cancel() noexcept
{
	c.InvokeCreateAborted();
	delete this;
}

void
MyStockClass::Create(CreateStockItem c,
		     StockRequest request,
		     gcc_unused CancellablePointer &cancel_ptr)
{
	auto &partition = *(Partition *)request.get();

	if (partition.defer_create) {
		new DeferredRequest(partition, c, std::move(request),
				    cancel_ptr);
		return;
	}

	if (partition.next_error) {
		++partition.factory_failed;

		c.InvokeCreateError(partition.next_error);
	} else {
		++partition.factory_created;

		auto *item = new MyStockItem(c, std::move(request));
		item->InvokeCreateSuccess();
	}
}

MyStockItem::~MyStockItem() noexcept
{
	auto &partition = *(Partition *)request.get();
	++partition.destroyed;
}

TEST(MultiStock, Basic)
{
	Instance instance;

	Partition foo{instance, "foo"};

	// request item, wait for it to be delivered

	foo.Get();
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);

	// request 3 more items (2 more than is allowed)

	foo.Get();
	foo.Get();
	foo.Get();

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 4);
	ASSERT_EQ(foo.waiting, 2);
	ASSERT_EQ(foo.ready, 2);
	ASSERT_EQ(foo.failed, 0);

	// release the first item; 1 waiting item will be handled, 1 remains waiting

	foo.leases.pop_front();

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 3);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 2);
	ASSERT_EQ(foo.failed, 0);

	// mark the item dirty (cannot be reused, 1 still waiting)

	foo.PutDirty(1);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 2);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);

	// release all other leases; a new item will be created

	foo.PutReady();

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, GetTooMany)
{
	Instance instance;

	Partition foo{instance, "foo"};

	/* request one more than allowed; this used to trigger an
	   assertion failure */
	foo.Get(3);

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 3);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 2);
	ASSERT_EQ(foo.failed, 0);

	foo.PutDirty(2);

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, DeferredCancel)
{
	Instance instance;

	Partition foo{instance, "foo"};
	foo.defer_create = true;

	foo.Get(16);

	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 16);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	foo.leases.clear();
	instance.RunSome();

	ASSERT_EQ(foo.total, 0);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, DeferredWaitingCancel)
{
	Instance instance;

	Partition foo{instance, "foo"};
	foo.defer_create = true;

	foo.Get(16);

	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 16);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	instance.RunSome();

	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 14);
	ASSERT_EQ(foo.ready, 2);
	ASSERT_EQ(foo.failed, 0);

	foo.leases.clear();
	instance.RunSome();

	ASSERT_EQ(foo.total, 0);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, Error)
{
	Instance instance;

	Partition foo{instance, "foo"};
	foo.next_error = std::make_exception_ptr(std::runtime_error{"Error"});

	foo.Get(16);

	ASSERT_EQ(foo.factory_created, 0);
	ASSERT_EQ(foo.factory_failed, 16);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 16);
}

TEST(MultiStock, DeferredError)
{
	Instance instance;

	Partition foo{instance, "foo"};
	foo.defer_create = true;
	foo.next_error = std::make_exception_ptr(std::runtime_error{"Error"});

	foo.Get(16);

	ASSERT_EQ(foo.factory_created, 0);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 16);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 0);
	ASSERT_EQ(foo.factory_failed, 1);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 16);
}

TEST(MultiStock, CreateTwo)
{
	Instance instance{2};

	Partition foo{instance, "foo"};
	foo.defer_create = true;

	foo.Get(16);

	ASSERT_EQ(foo.factory_created, 0);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 16);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 16);
	ASSERT_EQ(foo.waiting, 12);
	ASSERT_EQ(foo.ready, 4);
	ASSERT_EQ(foo.failed, 0);

	foo.PutReady(1);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 15);
	ASSERT_EQ(foo.waiting, 11);
	ASSERT_EQ(foo.ready, 4);
	ASSERT_EQ(foo.failed, 0);

	foo.PutReady(4);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 11);
	ASSERT_EQ(foo.waiting, 7);
	ASSERT_EQ(foo.ready, 4);
	ASSERT_EQ(foo.failed, 0);

	foo.PutReady(4);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 7);
	ASSERT_EQ(foo.waiting, 3);
	ASSERT_EQ(foo.ready, 4);
	ASSERT_EQ(foo.failed, 0);

	foo.PutDirty(1);
	foo.PutReady(1);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 3);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 5);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 4);
	ASSERT_EQ(foo.failed, 0);

	/* release all leases; one waiting request remains, but there
	   are two items; the MultiStock will assign one of them to
	   the waiting request, and will delete the other one */

	foo.PutReady();
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 3);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, FadeBusy)
{
	Instance instance;

	Partition foo{instance, "foo"};

	/* request one more than allowed */
	foo.Get(3);

	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 3);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 2);
	ASSERT_EQ(foo.failed, 0);

	/* enable "fade"; this means no change right now, because no
	   item is removed */
	instance.multi_stock.FadeAll();
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 3);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 2);
	ASSERT_EQ(foo.failed, 0);

	/* release one; the waiting client won't be handled because
	   the one item is in "fade" mode */
	foo.PutReady(1);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 2);
	ASSERT_EQ(foo.waiting, 1);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);

	/* release the last one; now the existing item will be
	   destroyed and a new one is created */
	foo.PutReady(1);
	instance.RunSome();

	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, FadeIdle)
{
	Instance instance;

	Partition foo{instance, "foo"};

	/* create one */
	foo.Get(1);
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);

	/* release it; it will remain idle */
	foo.PutReady(1);
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 0);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	/* fade it; the one idle item is destroyed now */
	instance.multi_stock.FadeAll();
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 0);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	/* request a new item */
	foo.Get(1);
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);
}

TEST(MultiStock, FadeOuter)
{
	Instance instance;

	Partition foo{instance, "foo"};

	/* create one */
	foo.Get(1);
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 0);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);

	/* release it, fade the outer item */
	foo.PutOuterDirty();
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 1);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 0);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 0);
	ASSERT_EQ(foo.failed, 0);

	/* request a new item */
	foo.Get(1);
	instance.RunSome();
	ASSERT_EQ(foo.factory_created, 2);
	ASSERT_EQ(foo.factory_failed, 0);
	ASSERT_EQ(foo.destroyed, 1);
	ASSERT_EQ(foo.total, 1);
	ASSERT_EQ(foo.waiting, 0);
	ASSERT_EQ(foo.ready, 1);
	ASSERT_EQ(foo.failed, 0);
}
