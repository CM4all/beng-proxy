// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "fs/FilteredSocket.hxx"

class EchoSocket final : public BufferedSocketHandler {
	FilteredSocket socket;

	bool close_after_data = false;

public:
	EchoSocket(EventLoop &_event_loop,
		   UniqueSocketDescriptor _fd, FdType _fd_type,
		   SocketFilterPtr _filter={}) noexcept;

	void Close() noexcept {
		socket.Close();
	}

	void CloseAfterData() noexcept {
		close_after_data = true;
	}

	/* virtual methods from BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};
