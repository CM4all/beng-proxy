// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_ITER_HXX
#define ODBUS_ITER_HXX

#include "Values.hxx"

namespace ODBus {
	class MessageIter {
		DBusMessageIter iter;

	public:
		MessageIter(DBusMessage &msg) {
			dbus_message_iter_init_append(&msg, &iter);
		}

		MessageIter(MessageIter &parent, int type,
			    const char *contained_signature) {
			if (!dbus_message_iter_open_container(&parent.iter, type,
							      contained_signature, &iter))
				throw std::runtime_error("dbus_message_iter_open_container() failed");
		}

		MessageIter(const MessageIter &) = delete;
		MessageIter &operator=(const MessageIter &) = delete;

		MessageIter &CloseContainer(MessageIter &parent) {
			if (!dbus_message_iter_close_container(&parent.iter, &iter))
				throw std::runtime_error("dbus_message_iter_close_container() failed");

			return parent;
		}

		MessageIter &AppendBasic(int type, const void *value) {
			if (!dbus_message_iter_append_basic(&iter, type, value))
				throw std::runtime_error("dbus_message_iter_append_basic() failed");
			return *this;
		}

		MessageIter &Append(const char *const&value) {
			return AppendBasic(DBUS_TYPE_STRING, &value);
		}

		MessageIter &Append(const uint32_t &value) {
			return AppendBasic(DBUS_TYPE_UINT32, &value);
		}

		MessageIter &AppendFixedArray(int element_type,
					      const void *value,
					      int n_elements) {
			if (!dbus_message_iter_append_fixed_array(&iter, element_type,
								  &value, n_elements))
				throw std::runtime_error("dbus_message_iter_append_fixed_array() failed");

			return *this;
		};

		MessageIter &AppendFixedArray(ConstBuffer<uint32_t> value) {
			return AppendFixedArray(DBUS_TYPE_UINT32,
						value.data, value.size);
		}

		MessageIter &Append(ConstBuffer<uint32_t> value) {
			return MessageIter(*this, DBUS_TYPE_ARRAY,
					   DBUS_TYPE_UINT32_AS_STRING)
				.AppendFixedArray(value)
				.CloseContainer(*this);
		}

		template<typename T>
		MessageIter &AppendEmptyArray() {
			return MessageIter(*this, DBUS_TYPE_ARRAY,
					   T::TypeAsString::value)
				.CloseContainer(*this);
		}

		template<typename T>
		MessageIter &AppendVariant(const char *contained_signature,
					   T &&value) {
			return MessageIter(*this, DBUS_TYPE_VARIANT,
					   contained_signature)
				.Append(std::forward<T>(value))
				.CloseContainer(*this);
		}

		template<typename T>
		MessageIter &AppendVariant(const T &value) {
			typedef VariantTypeTraits Traits;
			return MessageIter(*this, Traits::TYPE,
					   Traits::TypeAsString::value)
				.Append(value)
				.CloseContainer(*this);
		}

		template<typename T>
		MessageIter &Append(BasicValue<T> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;

			return AppendBasic(Traits::TYPE, &value.value);
		}

		template<typename T>
		MessageIter &Append(WrapVariant<T> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;
			typedef typename W::ContainedTraits ContainedTraits;

			return MessageIter(*this, Traits::TYPE,
					   ContainedTraits::TypeAsString::value)
				.Append(value.value)
				.CloseContainer(*this);
		}

		template<typename T>
		MessageIter &Append(WrapFixedArray<T> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;
			typedef typename W::ContainedTraits ContainedTraits;

			return MessageIter(*this, Traits::TYPE,
					   ContainedTraits::TypeAsString::value)
				.AppendFixedArray(value.value)
				.CloseContainer(*this);
		}

		template<size_t i, typename... T>
		struct _AppendTuple {
			MessageIter &operator()(MessageIter &iter, std::tuple<T...> value) {
				return _AppendTuple<i - 1, T...>()(iter.Append(std::get<sizeof...(T) - i>(value)),
								   value);
			}
		};

		template<typename... T>
		struct _AppendTuple<0, T...> {
			MessageIter &operator()(MessageIter &iter, std::tuple<T...>) {
				return iter;
			}
		};

		template<typename... T>
		MessageIter &AppendTuple(std::tuple<T...> value) {
			return _AppendTuple<sizeof...(T), T...>()(*this, value);
		}

		template<typename... T>
		MessageIter &Append(WrapStruct<T...> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;

			return MessageIter(*this, Traits::TYPE, nullptr)
				.AppendTuple(value.values)
				.CloseContainer(*this);
		}

		const char *GetSignature() {
			return dbus_message_iter_get_signature(&iter);
		}
	};
}

#endif
