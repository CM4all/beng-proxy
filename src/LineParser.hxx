/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LINE_PARSER_HXX
#define LINE_PARSER_HXX

#include "util/StringUtil.hxx"
#include "util/CharUtil.hxx"

#include <stdexcept>

class LineParser {
    char *p;

public:
    using Error = std::runtime_error;

    explicit LineParser(char *_p):p(StripLeft(_p)) {
        StripRight(p);
    }

    LineParser(const LineParser &) = delete;
    LineParser &operator=(const LineParser &) = delete;

    void Strip() {
        p = StripLeft(p);
    }

    char front() const {
        return *p;
    }

    bool IsEnd() const {
        return front() == 0;
    }

    void ExpectWhitespace() {
        if (!IsWhitespaceNotNull(front()))
            throw std::runtime_error("Syntax error");

        ++p;
        Strip();
    }

    void ExpectEnd() {
        if (!IsEnd())
            throw Error(std::string("Unexpected tokens at end of line: ") + p);
    }

    void ExpectSymbolAndEol(char symbol) {
        if (front() != symbol)
            throw Error(std::string("'") + symbol + "' expected");

        ++p;
        if (!IsEnd())
            throw Error(std::string("Unexpected tokens after '")
                        + symbol + "': " + p);
    }

    bool SkipSymbol(char symbol) {
        bool found = front() == symbol;
        if (found)
            ++p;
        return found;
    }

    bool SkipSymbol(char a, char b) {
        bool found = p[0] == a && p[1] == b;
        if (found)
            p += 2;
        return found;
    }

    /**
     * If the next word matches the given parameter, then skip it and
     * return true.  If not, the method returns false, leaving the
     * object unmodified.
     */
    bool SkipWord(const char *word);

    const char *NextWord();
    char *NextValue();
    char *NextUnescape();

    bool NextBool();
    unsigned NextPositiveInteger();

    /**
     * Expect a non-empty value.
     */
    char *ExpectValue();

    /**
     * Expect a non-empty value and end-of-line.
     */
    char *ExpectValueAndEnd();

private:
    char *NextUnquotedValue();
    char *NextQuotedValue(char stop);

    static constexpr bool IsWordChar(char ch) {
        return IsAlphaNumericASCII(ch) || ch == '_';
    }

    static constexpr bool IsUnquotedChar(char ch) {
        return IsWordChar(ch) || ch == '.' || ch == '-' || ch == ':';
    }

    static constexpr bool IsQuote(char ch) {
        return ch == '"' || ch == '\'';
    }
};

#endif
