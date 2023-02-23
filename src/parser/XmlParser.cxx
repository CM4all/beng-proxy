// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "XmlParser.hxx"
#include "HtmlSyntax.hxx"
#include "util/CharUtil.hxx"
#include "util/Poison.hxx"

#include <string.h>

XmlParser::XmlParser(struct pool &pool,
		     XmlParserHandler &_handler) noexcept
	:attr_value(pool, 512, 8192),
	 handler(_handler)
{
}

inline void
XmlParser::InvokeAttributeFinished() noexcept
{
	attr.name = {attr_name, attr_name_length};
	attr.value = attr_value.ReadStringView();

	handler.OnXmlAttributeFinished(attr);
	PoisonUndefinedT(attr);
}

size_t
XmlParser::Feed(const char *start, size_t length) noexcept
{
	const char *buffer = start, *end = start + length, *p;
	size_t nbytes;

	assert(buffer != nullptr);
	assert(length > 0);

	while (buffer < end) {
		switch (state) {
		case State::NONE:
		case State::SCRIPT:
			/* find first character */
			p = (const char *)memchr(buffer, '<', end - buffer);
			if (p == nullptr) {
				nbytes = handler.OnXmlCdata({buffer, end}, true,
							    position + buffer - start);
				assert(nbytes <= (size_t)(end - buffer));

				nbytes += buffer - start;
				position += (off_t)nbytes;
				return nbytes;
			}

			if (p > buffer) {
				nbytes = handler.OnXmlCdata({buffer, p}, true,
							    position + buffer - start);
				assert(nbytes <= (size_t)(p - buffer));

				if (nbytes < (size_t)(p - buffer)) {
					nbytes += buffer - start;
					position += (off_t)nbytes;
					return nbytes;
				}
			}

			tag.start = position + (off_t)(p - start);
			state = state == State::NONE
				? State::ELEMENT_NAME
				: State::SCRIPT_ELEMENT_NAME;
			tag_name_length = 0;
			tag.type = XmlParserTagType::OPEN;
			buffer = p + 1;
			break;

		case State::SCRIPT_ELEMENT_NAME:
			if (*buffer == '/') {
				state = State::ELEMENT_NAME;
				tag.type = XmlParserTagType::CLOSE;
				++buffer;
			} else {
				nbytes = handler.OnXmlCdata("<", true,
							    position + buffer - start);
				assert(nbytes <= (size_t)(end - buffer));

				if (nbytes == 0) {
					nbytes = buffer - start;
					position += nbytes;
					return nbytes;
				}

				state = State::SCRIPT;
			}

			break;

		case State::ELEMENT_NAME:
			/* copy element name */
			while (buffer < end) {
				if (is_html_name_char(*buffer)) {
					if (tag_name_length == sizeof(tag_name)) {
						/* name buffer overflowing */
						state = State::NONE;
						break;
					}

					tag_name[tag_name_length++] = ToLowerASCII(*buffer++);
				} else if (*buffer == '/' && tag_name_length == 0) {
					tag.type = XmlParserTagType::CLOSE;
					++buffer;
				} else if (*buffer == '?' && tag_name_length == 0) {
					/* start of processing instruction */
					tag.type = XmlParserTagType::PI;
					++buffer;
				} else if ((IsWhitespaceOrNull(*buffer) || *buffer == '/' ||
					    *buffer == '?' || *buffer == '>') &&
					   tag_name_length > 0) {
					bool interesting;

					tag.name = {tag_name, tag_name_length};

					interesting = handler.OnXmlTagStart(tag);

					state = interesting ? State::ELEMENT_TAG : State::ELEMENT_BORING;
					break;
				} else if (*buffer == '!' && tag_name_length == 0) {
					state = State::DECLARATION_NAME;
					++buffer;
					break;
				} else {
					state = State::NONE;
					break;
				}
			}

			break;

		case State::ELEMENT_TAG:
			do {
				if (IsWhitespaceOrNull(*buffer)) {
					++buffer;
				} else if (*buffer == '/' && tag.type == XmlParserTagType::OPEN) {
					tag.type = XmlParserTagType::SHORT;
					state = State::SHORT;
					++buffer;
					break;
				} else if (*buffer == '?' && tag.type == XmlParserTagType::PI) {
					state = State::SHORT;
					++buffer;
					break;
				} else if (*buffer == '>') {
					state = State::INSIDE;
					++buffer;
					tag.end = position + (off_t)(buffer - start);

					if (!handler.OnXmlTagFinished(tag))
						return 0;

					PoisonUndefinedT(tag);
					break;
				} else if (is_html_name_start_char(*buffer)) {
					state = State::ATTR_NAME;
					attr.name_start = position + (off_t)(buffer - start);
					attr_name_length = 0;
					attr_value.Clear();
					break;
				} else {
					/* ignore this syntax error and just close the
					   element tag */

					tag.end = position + (off_t)(buffer - start);
					state = State::INSIDE;

					if (!handler.OnXmlTagFinished(tag))
						return 0;

					state = State::NONE;
					break;
				}
			} while (buffer < end);

			break;

		case State::ELEMENT_BORING:
			/* ignore this tag */

			p = (const char *)memchr(buffer, '>', end - buffer);
			if (p != nullptr) {
				/* the "boring" tag has been closed */
				buffer = p + 1;
				state = State::NONE;
			} else
				buffer = end;
			break;

		case State::ATTR_NAME:
			/* copy attribute name */
			do {
				if (is_html_name_char(*buffer)) {
					if (attr_name_length == sizeof(attr_name)) {
						/* name buffer overflowing */
						state = State::ELEMENT_TAG;
						break;
					}

					attr_name[attr_name_length++] = ToLowerASCII(*buffer++);
				} else {
					state = State::AFTER_ATTR_NAME;
					break;
				}
			} while (buffer < end);

			break;

		case State::AFTER_ATTR_NAME:
			/* wait till we find '=' */
			do {
				if (*buffer == '=') {
					state = State::BEFORE_ATTR_VALUE;
					++buffer;
					break;
				} else if (IsWhitespaceOrNull(*buffer)) {
					++buffer;
				} else {
					/* there is no value (probably malformed XML) -
					   use the current position as start and end
					   offset because that's the best we can do */
					attr.value_start = attr.value_end = position + (off_t)(buffer - start);

					InvokeAttributeFinished();
					state = State::ELEMENT_TAG;
					break;
				}
			} while (buffer < end);

			break;

		case State::BEFORE_ATTR_VALUE:
			do {
				if (*buffer == '"' || *buffer == '\'') {
					state = State::ATTR_VALUE;
					attr_value_delimiter = *buffer;
					++buffer;
					attr.value_start = position + (off_t)(buffer - start);
					break;
				} else if (IsWhitespaceOrNull(*buffer)) {
					++buffer;
				} else {
					state = State::ATTR_VALUE_COMPAT;
					attr.value_start = position + (off_t)(buffer - start);
					break;
				}
			} while (buffer < end);

			break;

		case State::ATTR_VALUE:
			/* wait till we find the delimiter */
			p = (const char *)memchr(buffer, attr_value_delimiter,
						 end - buffer);
			if (p == nullptr) {
				if (!attr_value.Write(buffer, end - buffer)) {
					state = State::ELEMENT_TAG;
					break;
				}

				buffer = end;
			} else {
				if (!attr_value.Write(buffer, p - buffer)) {
					state = State::ELEMENT_TAG;
					break;
				}

				buffer = p + 1;
				attr.end = position + (off_t)(buffer - start);
				attr.value_end = attr.end - 1;
				InvokeAttributeFinished();
				state = State::ELEMENT_TAG;
			}

			break;

		case State::ATTR_VALUE_COMPAT:
			/* wait till the value is finished */
			do {
				if (!IsWhitespaceOrNull(*buffer) && *buffer != '>') {
					if (!attr_value.Write(buffer, 1)) {
						state = State::ELEMENT_TAG;
						break;
					}

					++buffer;
				} else {
					attr.value_end = attr.end =
						position + (off_t)(buffer - start);
					InvokeAttributeFinished();
					state = State::ELEMENT_TAG;
					break;
				}
			} while (buffer < end);

			break;

		case State::SHORT:
			do {
				if (IsWhitespaceOrNull(*buffer)) {
					++buffer;
				} else if (*buffer == '>') {
					state = State::NONE;
					++buffer;
					tag.end = position + (off_t)(buffer - start);

					if (!handler.OnXmlTagFinished(tag))
						return 0;

					PoisonUndefinedT(tag);

					break;
				} else {
					/* ignore this syntax error and just close the
					   element tag */

					tag.end = position + (off_t)(buffer - start);
					state = State::INSIDE;

					if (!handler.OnXmlTagFinished(tag))
						return 0;

					PoisonUndefinedT(tag);
					state = State::NONE;

					break;
				}
			} while (buffer < end);

			break;

		case State::INSIDE:
			/* XXX */
			state = State::NONE;
			break;

		case State::DECLARATION_NAME:
			/* copy declaration element name */
			while (buffer < end) {
				if (IsAlphaNumericASCII(*buffer) || *buffer == ':' ||
				    *buffer == '-' || *buffer == '_' || *buffer == '[') {
					if (tag_name_length == sizeof(tag_name)) {
						/* name buffer overflowing */
						state = State::NONE;
						break;
					}

					tag_name[tag_name_length++] = ToLowerASCII(*buffer++);

					if (tag_name_length == 7 &&
					    memcmp(tag_name, "[cdata[", 7) == 0) {
						state = State::CDATA_SECTION;
						cdend_match = 0;
						break;
					}

					if (tag_name_length == 2 &&
					    memcmp(tag_name, "--", 2) == 0) {
						state = State::COMMENT;
						minus_count = 0;
						break;
					}
				} else {
					state = State::NONE;
					break;
				}
			}

			break;

		case State::CDATA_SECTION:
			/* copy CDATA section contents */

			/* XXX this loop can be optimized with memchr() */
			p = buffer;
			while (buffer < end) {
				if (*buffer == ']' && cdend_match < 2) {
					if (buffer > p) {
						/* flush buffer */

						size_t cdata_length = buffer - p;
						off_t cdata_end = position + buffer - start;
						off_t cdata_start = cdata_end - cdata_length;

						nbytes = handler.OnXmlCdata({p, cdata_length}, false,
									    cdata_start);
						assert(nbytes <= (size_t)(buffer - p));

						if (nbytes < (size_t)(buffer - p)) {
							nbytes += p - start;
							position += (off_t)nbytes;
							return nbytes;
						}
					}

					p = ++buffer;
					++cdend_match;
				} else if (*buffer == '>' && cdend_match == 2) {
					p = ++buffer;
					state = State::NONE;
					break;
				} else {
					if (cdend_match > 0) {
						/* we had a partial match, and now we have to
						   restore the data we already skipped */
						assert(cdend_match < 3);

						nbytes = handler.OnXmlCdata({"]]", cdend_match}, false,
									    position + buffer - start);
						assert(nbytes <= cdend_match);

						cdend_match -= nbytes;

						if (cdend_match > 0) {
							nbytes = buffer - start;
							position += (off_t)nbytes;
							return nbytes;
						}

						p = buffer;
					}

					++buffer;
				}
			}

			if (buffer > p) {
				size_t cdata_length = buffer - p;
				off_t cdata_end = position + buffer - start;
				off_t cdata_start = cdata_end - cdata_length;

				nbytes = handler.OnXmlCdata({p, cdata_length}, false,
							    cdata_start);
				assert(nbytes <= (size_t)(buffer - p));

				if (nbytes < (size_t)(buffer - p)) {
					nbytes += p - start;
					position += (off_t)nbytes;
					return nbytes;
				}
			}

			break;

		case State::COMMENT:
			switch (minus_count) {
			case 0:
				/* find a minus which introduces the "-->" sequence */
				p = (const char *)memchr(buffer, '-', end - buffer);
				if (p != nullptr) {
					/* found one - minus_count=1 and go to char after
					   minus */
					buffer = p + 1;
					minus_count = 1;
				} else
					/* none found - skip this chunk */
					buffer = end;

				break;

			case 1:
				if (*buffer == '-')
					/* second minus found */
					minus_count = 2;
				else
					minus_count = 0;
				++buffer;

				break;

			case 2:
				if (*buffer == '>')
					/* end of comment */
					state = State::NONE;
				else if (*buffer == '-')
					/* another minus... keep minus_count at 2 and go
					   to next character */
					++buffer;
				else
					minus_count = 0;

				break;
			}

			break;
		}
	}

	position += length;
	return length;
}
