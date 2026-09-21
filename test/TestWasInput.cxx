// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "was/Input.hxx"
#include "istream/Bucket.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "io/Pipe.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/SpanCast.hxx"

#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

using std::string_view_literals::operator""sv;

namespace {

static constexpr auto payload = "hello"sv;

class MySink final : public IstreamSink {
public:
	std::exception_ptr error;
	bool eof = false;

	explicit MySink(UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)) {}

	void FillBucketList(IstreamBucketList &list) {
		input.FillBucketList(list);
	}

private:
	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte>) noexcept override {
		return 0;
	}

	void OnEof() noexcept override {
		ClearInput();
		eof = true;
	}

	void OnError(std::exception_ptr &&_error) noexcept override {
		ClearInput();
		error = std::move(_error);
	}
};

/**
 * Emulates the #WasClient: WasInputRelease() fails, and destroys the
 * #WasInput before returning false (which is what
 * WasClient::AbortControlError() ends up doing when the PREMATURE
 * packet cannot be sent).
 */
class FailingReleaseHandler final : public WasInputHandler {
public:
	WasInput *input = nullptr;

	unsigned n_close = 0, n_release = 0, n_eof = 0, n_error = 0;

	/* virtual methods from class WasInputHandler */
	void WasInputClose(uint64_t) noexcept override {
		++n_close;
	}

	bool WasInputRelease() noexcept override {
		++n_release;

		was_input_free(std::exchange(input, nullptr),
			       std::make_exception_ptr(std::runtime_error{"control error"}));
		return false;
	}

	void WasInputEof() noexcept override {
		++n_eof;
	}

	void WasInputError() noexcept override {
		++n_error;
	}
};

} // anonymous namespace

/**
 * WasInputHandler::WasInputRelease() returning false means the
 * #WasInput has been destroyed and its handler has been notified
 * already; _FillBucketList() must not touch either of them again.
 */
TEST(WasInput, FillBucketListReleaseFails)
{
	TestInstance instance;

	auto [r, w] = CreatePipe();
	w.Write(AsBytes(payload));
	w.Close();

	FailingReleaseHandler handler;
	handler.input = was_input_new(instance.root_pool, instance.event_loop,
				      r, handler);

	/* announce the exact length, so that reading the payload
	   makes CanRelease() true */
	ASSERT_TRUE(was_input_set_length(handler.input, payload.size()));

	MySink sink{was_input_enable(*handler.input)};

	IstreamBucketList list;
	EXPECT_THROW(sink.FillBucketList(list), std::runtime_error);

	EXPECT_EQ(handler.n_release, 1u);

	/* WasInputRelease() reported the error already; calling
	   WasInputError() afterwards would run into the destroyed
	   WasClient */
	EXPECT_EQ(handler.n_error, 0u);

	EXPECT_TRUE(sink.error);
	EXPECT_FALSE(sink.eof);
}
