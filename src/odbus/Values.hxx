// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_VALUES_HXX
#define ODBUS_VALUES_HXX

#include "Types.hxx"
#include "util/ConstBuffer.hxx"

#include <dbus/dbus.h>

#include <tuple>

namespace ODBus {
	template<typename T>
	struct BasicValue {
		typedef TypeTraits<T> Traits;
		const T &value;

		explicit constexpr BasicValue(const T &_value)
		:value(_value) {}
	};

	struct String : BasicValue<const char *> {
		explicit constexpr String(const char *const&_value)
		:BasicValue(_value) {}
	};

	struct Boolean {
		typedef BooleanTypeTraits Traits;
		dbus_bool_t value;

		explicit constexpr Boolean(bool _value)
			:value(_value) {}
	};

	template<typename T, template<typename U> class WrapTraits>
	struct WrapValue {
		typedef typename T::Traits ContainedTraits;
		typedef WrapTraits<ContainedTraits> Traits;
		const T &value;

		explicit constexpr WrapValue(const T &_value)
		:value(_value) {}
	};

	template<typename T>
	struct WrapVariant : BasicValue<T> {
		typedef typename T::Traits ContainedTraits;
		typedef VariantTypeTraits Traits;

		explicit constexpr WrapVariant(const T &_value)
		:BasicValue<T>(_value) {}
	};

	template<typename T>
	static WrapVariant<T> Variant(const T &_value) {
		return WrapVariant<T>(_value);
	};

	template<typename T>
	struct WrapFixedArray {
		typedef TypeTraits<T> ContainedTraits;
		typedef ArrayTypeTraits<ContainedTraits> Traits;
		ConstBuffer<T> value;

		explicit constexpr WrapFixedArray(const T *_data, size_t _size)
		:value(_data, _size) {}
	};

	template<typename T>
	static WrapFixedArray<T> FixedArray(const T *_data,
					    size_t _size) {
		return WrapFixedArray<T>(_data, _size);
	};

	template<typename... T>
	struct WrapStruct {
		typedef StructTypeTraits<T...> Traits;

		std::tuple<const T&...> values;

		explicit constexpr WrapStruct(const T&... _values)
		:values(_values...) {}
	};

	template<typename... T>
	static WrapStruct<T...> Struct(const T&... values) {
		return WrapStruct<T...>(values...);
	};
}

#endif
