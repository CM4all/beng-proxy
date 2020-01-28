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

#include "Stock.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

#include <string>

#include <string.h>

struct NfsStockConnection;

struct NfsStockRequest final
	: boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
	  Cancellable {
	NfsStockConnection &connection;

	NfsStockGetHandler &handler;

	NfsStockRequest(NfsStockConnection &_connection,
			NfsStockGetHandler &_handler,
			CancellablePointer &cancel_ptr) noexcept
		:connection(_connection),
		 handler(_handler) {
		cancel_ptr = *this;
	}

	void Destroy() noexcept {
		this->~NfsStockRequest();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;
};

struct NfsStockConnection final
	: NfsClientHandler,
	  boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	NfsStock &stock;

	const std::string key;

	NfsClient *client;

	CancellablePointer cancel_ptr;

	boost::intrusive::list<NfsStockRequest,
			       boost::intrusive::constant_time_size<false>> requests;

	NfsStockConnection(NfsStock &_stock, const char *_key) noexcept
		:stock(_stock), key(_key), client(nullptr) {}

	void Remove(NfsStockRequest &r) noexcept {
		assert(!requests.empty());

		requests.erase(requests.iterator_to(r));
	}

	/* virtual methods from NfsClientHandler */
	void OnNfsClientReady(NfsClient &client) noexcept override;
	void OnNfsMountError(std::exception_ptr ep) noexcept override;
	void OnNfsClientClosed(std::exception_ptr ep) noexcept override;

	struct Compare {
		bool operator()(const NfsStockConnection &a, const NfsStockConnection &b) const noexcept {
			return a.key < b.key;
		}

		bool operator()(const NfsStockConnection &a, const char *b) const noexcept {
			return a.key < b;
		}

		bool operator()(const char *a, const NfsStockConnection &b) const noexcept {
			return a < b.key;
		}
	};
};

class NfsStock final {
	EventLoop &event_loop;

	/**
	 * Maps server name to #NfsStockConnection.
	 */
	typedef boost::intrusive::set<NfsStockConnection,
				      boost::intrusive::compare<NfsStockConnection::Compare>,
				      boost::intrusive::constant_time_size<false>> ConnectionMap;
	ConnectionMap connections;

public:
	explicit NfsStock(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	~NfsStock() noexcept;

	void Get(AllocatorPtr alloc,
		 const char *server, const char *export_name,
		 NfsStockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	void Remove(NfsStockConnection &c) noexcept {
		assert(!connections.empty());

		connections.erase(connections.iterator_to(c));
	}
};

/*
 * NfsClientHandler
 *
 */

void
NfsStockConnection::OnNfsClientReady(NfsClient &_client) noexcept
{
	assert(client == nullptr);

	client = &_client;

	requests.clear_and_dispose([&_client](NfsStockRequest *request){
		request->handler.OnNfsStockReady(_client);
		request->Destroy();
	});
}

void
NfsStockConnection::OnNfsMountError(std::exception_ptr ep) noexcept
{
	requests.clear_and_dispose([&ep](NfsStockRequest *request){
		request->handler.OnNfsStockError(ep);
		request->Destroy();
	});

	stock.Remove(*this);
	delete this;
}

void
NfsStockConnection::OnNfsClientClosed(std::exception_ptr ep) noexcept
{
	assert(requests.empty());

	LogConcat(1, key.c_str(), "NFS connection closed: ", ep);

	stock.Remove(*this);
	delete this;
}

/*
 * async operation
 *
 */

void
NfsStockRequest::Cancel() noexcept
{
	connection.Remove(*this);
	Destroy();

	// TODO: abort client if all requests are gone?
}

/*
 * public
 *
 */

NfsStock *
nfs_stock_new(EventLoop &event_loop) noexcept
{
	return new NfsStock(event_loop);
}

NfsStock::~NfsStock() noexcept
{
	connections.clear_and_dispose([](NfsStockConnection *connection){
		if (connection->client != nullptr)
			nfs_client_free(connection->client);
		else
			connection->cancel_ptr.Cancel();

		assert(connection->requests.empty());
		delete connection;
	});
}

void
nfs_stock_free(NfsStock *stock) noexcept
{
	delete stock;
}

inline void
NfsStock::Get(AllocatorPtr alloc,
	      const char *server, const char *export_name,
	      NfsStockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	const char *key = alloc.Concat(server, ':', export_name);

	ConnectionMap::insert_commit_data hint;
	auto result = connections.insert_check(key,
					       NfsStockConnection::Compare(),
					       hint);
	const bool is_new = result.second;
	NfsStockConnection *connection;
	if (is_new) {
		connection = new NfsStockConnection(*this, key);

		connections.insert_commit(*connection, hint);
	} else {
		connection = &*result.first;
		if (connection->client != nullptr) {
			/* already connected */
			handler.OnNfsStockReady(*connection->client);
			return;
		}
	}

	auto request = alloc.New<NfsStockRequest>(*connection,
						  handler, cancel_ptr);
	connection->requests.push_front(*request);

	if (is_new)
		nfs_client_new(connection->stock.event_loop,
			       server, export_name,
			       *connection, connection->cancel_ptr);
}

void
nfs_stock_get(NfsStock *stock, AllocatorPtr alloc,
	      const char *server, const char *export_name,
	      NfsStockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	stock->Get(alloc, server, export_name, handler, cancel_ptr);
}
