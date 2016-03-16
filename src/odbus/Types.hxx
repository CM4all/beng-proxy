// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_TYPES_HXX
#define ODBUS_TYPES_HXX

#include "util/TemplateString.hxx"

#include <dbus/dbus.h>

namespace ODBus {
	template<int type>
	struct BasicTypeTraits {
		static constexpr int TYPE = type;
		typedef TemplateString::CharAsString<TYPE> TypeAsString;
	};

	template<typename T>
	struct TypeTraits {
	};

	template<>
	struct TypeTraits<const char *> : BasicTypeTraits<DBUS_TYPE_STRING> {
	};

	using StringTypeTraits = TypeTraits<const char *>;

	template<>
	struct TypeTraits<dbus_uint32_t> : BasicTypeTraits<DBUS_TYPE_UINT32> {
	};

	using BooleanTypeTraits = BasicTypeTraits<DBUS_TYPE_BOOLEAN>;

	template<typename T>
	struct ArrayTypeTraits {
		typedef T ContainedTraits;

		static constexpr int TYPE = DBUS_TYPE_ARRAY;
		typedef TemplateString::InsertBefore<TYPE, typename ContainedTraits::TypeAsString> TypeAsString;
	};

	using VariantTypeTraits = BasicTypeTraits<DBUS_TYPE_VARIANT>;

	template<typename T, typename... ContainedTraits>
	struct _MakeStructTypeAsString
		: TemplateString::Concat<typename T::TypeAsString,
					 _MakeStructTypeAsString<ContainedTraits...>> {};

	template<typename T>
	struct _MakeStructTypeAsString<T> : T::TypeAsString {};

	template<typename... ContainedTraits>
	struct StructTypeTraits {
		static constexpr int TYPE = DBUS_TYPE_STRUCT;

		typedef TemplateString::Concat<TemplateString::CharAsString<DBUS_STRUCT_BEGIN_CHAR>,
					       _MakeStructTypeAsString<ContainedTraits...>,
					       TemplateString::CharAsString<DBUS_STRUCT_END_CHAR>> TypeAsString;
	};
}

#endif
