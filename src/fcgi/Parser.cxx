// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Parser.hxx"
#include "Protocol.hxx"

#include <cassert>

FcgiParser::FeedResult
FcgiParser::Feed(std::span<const std::byte> src, FcgiFrameHandler &handler)
{
	while (true) {
		if (remaining > 0) {
			assert(in_frame);

			if (src.empty())
				return FeedResult::MORE;

			auto payload = src;
			if (payload.size() > remaining)
				payload = payload.first(remaining);

			const auto [result, consumed] = handler.OnFramePayload(payload);
			if (consumed == 0) [[unlikely]] {
				switch (result) {
				case FcgiFrameHandler::FrameResult::SKIP:
					SkipCurrent();
					continue;

				case FcgiFrameHandler::FrameResult::CONTINUE:
					return FeedResult::BLOCKING;

				case FcgiFrameHandler::FrameResult::STOP:
					return FeedResult::STOP;

				case FcgiFrameHandler::FrameResult::CLOSED:
					return FeedResult::CLOSED;
				}
			}

			src = src.subspan(consumed);
			handler.OnFrameConsumed(consumed);
			remaining -= consumed;

			switch (result) {
			case FcgiFrameHandler::FrameResult::SKIP:
				SkipCurrent();
				continue;

			case FcgiFrameHandler::FrameResult::CONTINUE:
				if (remaining > 0)
					return consumed < payload.size()
					       ? FeedResult::BLOCKING
					       : FeedResult::MORE;

				break;

			case FcgiFrameHandler::FrameResult::STOP:
				return FeedResult::STOP;

			case FcgiFrameHandler::FrameResult::CLOSED:
				return FeedResult::CLOSED;
			}
		}

		if (skip > 0) {
			assert(in_frame);

			if (skip > src.size()) {
				const std::size_t consumed = src.size();
				skip -= consumed;
				handler.OnFrameConsumed(consumed);
				return FeedResult::MORE;
			}

			const std::size_t consumed = skip;
			handler.OnFrameConsumed(consumed);
			src = src.subspan(consumed);
			skip = 0;
		}

		if (in_frame) {
			in_frame = false;

			switch (handler.OnFrameEnd()) {
			case FcgiFrameHandler::FrameResult::SKIP:
			case FcgiFrameHandler::FrameResult::CONTINUE:
				break;

			case FcgiFrameHandler::FrameResult::STOP:
				return FeedResult::STOP;

			case FcgiFrameHandler::FrameResult::CLOSED:
				return FeedResult::CLOSED;
			}
		}

		const auto &header =
			*reinterpret_cast<const FcgiRecordHeader *>(src.data());
		if (src.size() < sizeof(header))
			return FeedResult::MORE;

		in_frame = true;
		remaining = header.content_length;
		skip = header.padding_length;

		const auto type = header.type;
		const uint_least16_t request_id = header.request_id;

		handler.OnFrameConsumed(sizeof(header));
		src = src.subspan(sizeof(header));

		switch (handler.OnFrameHeader(type, request_id)) {
		case FcgiFrameHandler::FrameResult::SKIP:
			SkipCurrent();
			break;

		case FcgiFrameHandler::FrameResult::CONTINUE:
			break;

		case FcgiFrameHandler::FrameResult::STOP:
			return FeedResult::STOP;

		case FcgiFrameHandler::FrameResult::CLOSED:
			return FeedResult::CLOSED;
		}

	}
}
