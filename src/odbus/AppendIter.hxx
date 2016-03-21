// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_APPEND_ITER_HXX
#define ODBUS_APPEND_ITER_HXX

#include "Iter.hxx"
#include "Values.hxx"

namespace ODBus {
	class AppendMessageIter : public MessageIter {
	public:
		explicit AppendMessageIter(DBusMessage &msg) {
			dbus_message_iter_init_append(&msg, &iter);
		}

		AppendMessageIter(AppendMessageIter &parent, int type,
				  const char *contained_signature) {
			if (!dbus_message_iter_open_container(&parent.iter, type,
							      contained_signature, &iter))
				throw std::runtime_error("dbus_message_iter_open_container() failed");
		}

		AppendMessageIter &CloseContainer(AppendMessageIter &parent) {
			if (!dbus_message_iter_close_container(&parent.iter, &iter))
				throw std::runtime_error("dbus_message_iter_close_container() failed");

			return parent;
		}

		AppendMessageIter &AppendBasic(int type, const void *value) {
			if (!dbus_message_iter_append_basic(&iter, type, value))
				throw std::runtime_error("dbus_message_iter_append_basic() failed");
			return *this;
		}

		AppendMessageIter &Append(const char *const&value) {
			return AppendBasic(DBUS_TYPE_STRING, &value);
		}

		AppendMessageIter &Append(const uint32_t &value) {
			return AppendBasic(DBUS_TYPE_UINT32, &value);
		}

		AppendMessageIter &AppendFixedArray(int element_type,
						    const void *value,
						    int n_elements) {
			if (!dbus_message_iter_append_fixed_array(&iter, element_type,
								  &value, n_elements))
				throw std::runtime_error("dbus_message_iter_append_fixed_array() failed");

			return *this;
		};

		AppendMessageIter &AppendFixedArray(ConstBuffer<uint32_t> value) {
			return AppendFixedArray(DBUS_TYPE_UINT32,
						value.data, value.size);
		}

		AppendMessageIter &Append(ConstBuffer<uint32_t> value) {
			return AppendMessageIter(*this, DBUS_TYPE_ARRAY,
						 DBUS_TYPE_UINT32_AS_STRING)
				.AppendFixedArray(value)
				.CloseContainer(*this);
		}

		template<typename T>
		AppendMessageIter &AppendEmptyArray() {
			return AppendMessageIter(*this, DBUS_TYPE_ARRAY,
						 T::TypeAsString::value)
				.CloseContainer(*this);
		}

		template<typename T>
		AppendMessageIter &AppendVariant(const char *contained_signature,
						 T &&value) {
			return AppendMessageIter(*this, DBUS_TYPE_VARIANT,
						 contained_signature)
				.Append(std::forward<T>(value))
				.CloseContainer(*this);
		}

		template<typename T>
		AppendMessageIter &AppendVariant(const T &value) {
			typedef VariantTypeTraits Traits;
			return AppendMessageIter(*this, Traits::TYPE,
						 Traits::TypeAsString::value)
				.Append(value)
				.CloseContainer(*this);
		}

		template<typename T>
		AppendMessageIter &Append(BasicValue<T> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;

			return AppendBasic(Traits::TYPE, &value.value);
		}

		AppendMessageIter &Append(const Boolean &value) {
			typedef typename Boolean::Traits Traits;

			return AppendBasic(Traits::TYPE, &value.value);
		}

		template<typename T>
		AppendMessageIter &Append(WrapVariant<T> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;
			typedef typename W::ContainedTraits ContainedTraits;

			return AppendMessageIter(*this, Traits::TYPE,
						 ContainedTraits::TypeAsString::value)
				.Append(value.value)
				.CloseContainer(*this);
		}

		template<typename T>
		AppendMessageIter &Append(WrapFixedArray<T> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;
			typedef typename W::ContainedTraits ContainedTraits;

			return AppendMessageIter(*this, Traits::TYPE,
						 ContainedTraits::TypeAsString::value)
				.AppendFixedArray(value.value)
				.CloseContainer(*this);
		}

		template<size_t i, typename... T>
		struct _AppendTuple {
			AppendMessageIter &operator()(AppendMessageIter &iter, std::tuple<T...> value) {
				return _AppendTuple<i - 1, T...>()(iter.Append(std::get<sizeof...(T) - i>(value)),
								   value);
			}
		};

		template<typename... T>
		struct _AppendTuple<0, T...> {
			AppendMessageIter &operator()(AppendMessageIter &iter, std::tuple<T...>) {
				return iter;
			}
		};

		template<typename... T>
		AppendMessageIter &AppendTuple(std::tuple<T...> value) {
			return _AppendTuple<sizeof...(T), T...>()(*this, value);
		}

		template<typename... T>
		AppendMessageIter &Append(WrapStruct<T...> value) {
			typedef decltype(value) W;
			typedef typename W::Traits Traits;

			return AppendMessageIter(*this, Traits::TYPE, nullptr)
				.AppendTuple(value.values)
				.CloseContainer(*this);
		}
	};
}

#endif
