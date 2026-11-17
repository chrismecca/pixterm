// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/json.hpp>

#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <format>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace pixterm::json {

std::optional<bool> Value::as_bool() const noexcept {
    if (const auto* b = std::get_if<bool>(&data_))
        return *b;
    return std::nullopt;
}

std::optional<double> Value::as_number() const noexcept {
    if (const auto* d = std::get_if<double>(&data_))
        return *d;
    return std::nullopt;
}

std::optional<std::int64_t> Value::as_integer() const noexcept {
    constexpr double limit = 9007199254740992.0; // 2^53
    const auto d = as_number();
    if (!d || std::trunc(*d) != *d || std::fabs(*d) > limit)
        return std::nullopt;
    return static_cast<std::int64_t>(*d);
}

std::optional<std::string_view> Value::as_string() const noexcept {
    if (const auto* s = std::get_if<std::string>(&data_))
        return *s;
    return std::nullopt;
}

const Array* Value::as_array() const noexcept {
    return std::get_if<Array>(&data_);
}

const Object* Value::as_object() const noexcept {
    return std::get_if<Object>(&data_);
}

const Value* Value::find(std::string_view key) const noexcept {
    if (const auto* object = as_object()) {
        for (const auto& m : *object)
            if (m.key == key)
                return &m.value;
    }
    return nullptr;
}

namespace {

void append_utf8(std::string& out, char32_t c) {
    const auto byte = [&out](unsigned v) { out.push_back(static_cast<char>(v)); };
    if (c < 0x80) {
        byte(c);
    } else if (c < 0x800) {
        byte(0xc0 | (c >> 6));
        byte(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        byte(0xe0 | (c >> 12));
        byte(0x80 | ((c >> 6) & 0x3f));
        byte(0x80 | (c & 0x3f));
    } else {
        byte(0xf0 | (c >> 18));
        byte(0x80 | ((c >> 12) & 0x3f));
        byte(0x80 | ((c >> 6) & 0x3f));
        byte(0x80 | (c & 0x3f));
    }
}

// Decodes one well-formed UTF-8 sequence at the start of s, rejecting
// overlong forms and surrogates. Returns the code point and its length.
std::optional<std::pair<char32_t, std::size_t>> decode_utf8(std::string_view s) {
    const auto at = [&s](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned lead = at(0);
    std::size_t length = 0;
    char32_t c = 0;
    char32_t min = 0;
    if (lead < 0x80)
        return std::pair{static_cast<char32_t>(lead), std::size_t{1}};
    if ((lead & 0xe0) == 0xc0) {
        length = 2;
        c = lead & 0x1f;
        min = 0x80;
    } else if ((lead & 0xf0) == 0xe0) {
        length = 3;
        c = lead & 0x0f;
        min = 0x800;
    } else if ((lead & 0xf8) == 0xf0) {
        length = 4;
        c = lead & 0x07;
        min = 0x10000;
    } else {
        return std::nullopt;
    }
    if (s.size() < length)
        return std::nullopt;
    for (std::size_t i = 1; i < length; ++i) {
        if ((at(i) & 0xc0) != 0x80)
            return std::nullopt;
        c = (c << 6) | (at(i) & 0x3f);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        return std::nullopt;
    return std::pair{c, length};
}

// Converts text that already matches the JSON number grammar. libc++ 19
// has no floating-point from_chars, and strtod follows the locale's
// decimal point, so this uses strtod_l with the C locale.
std::optional<double> to_double(std::string_view digits) {
    static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
    const std::string text(digits);
    char* end = nullptr;
    const double d = strtod_l(text.c_str(), &end, c_locale);
    if (c_locale == nullptr || end != text.c_str() + text.size() || !std::isfinite(d))
        return std::nullopt;
    return d;
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::expected<Value, ParseError> document() {
        auto v = value(0);
        if (!v)
            return v;
        skip_space();
        if (pos_ != text_.size())
            return fail("unexpected text after the value");
        return v;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;

    [[nodiscard]] std::unexpected<ParseError> fail(std::string message) const {
        return std::unexpected(ParseError{.offset = pos_, .message = std::move(message)});
    }

    [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const { return at_end() ? '\0' : text_[pos_]; }

    void skip_space() {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
            ++pos_;
    }

    bool consume(std::string_view word) {
        if (!text_.substr(pos_).starts_with(word))
            return false;
        pos_ += word.size();
        return true;
    }

    std::expected<Value, ParseError> value(int depth) {
        skip_space();
        if (depth > max_depth)
            return fail("nested too deeply");
        switch (peek()) {
        case '{':
            return object(depth + 1);
        case '[':
            return array(depth + 1);
        case '"': {
            auto s = string();
            if (!s)
                return std::unexpected(s.error());
            return Value(std::move(*s));
        }
        case 't':
            if (consume("true"))
                return Value(true);
            break;
        case 'f':
            if (consume("false"))
                return Value(false);
            break;
        case 'n':
            if (consume("null"))
                return Value();
            break;
        default:
            if (peek() == '-' || ascii::is_digit(peek()))
                return number();
            break;
        }
        return fail("expected a value");
    }

    std::expected<Value, ParseError> object(int depth) {
        Object members;
        ++pos_; // '{'
        skip_space();
        if (consume("}"))
            return Value(std::move(members));
        for (;;) {
            skip_space();
            if (peek() != '"')
                return fail("expected a member name");
            auto key = string();
            if (!key)
                return std::unexpected(key.error());
            for (const auto& m : members)
                if (m.key == *key)
                    return fail(std::format("member \"{}\" appears twice", *key));
            skip_space();
            if (!consume(":"))
                return fail("expected ':'");
            auto v = value(depth);
            if (!v)
                return v;
            members.push_back({std::move(*key), std::move(*v)});
            skip_space();
            if (consume("}"))
                return Value(std::move(members));
            if (!consume(","))
                return fail("expected ',' or '}'");
        }
    }

    std::expected<Value, ParseError> array(int depth) {
        Array elements;
        ++pos_; // '['
        skip_space();
        if (consume("]"))
            return Value(std::move(elements));
        for (;;) {
            auto v = value(depth);
            if (!v)
                return v;
            elements.push_back(std::move(*v));
            skip_space();
            if (consume("]"))
                return Value(std::move(elements));
            if (!consume(","))
                return fail("expected ',' or ']'");
        }
    }

    // The RFC 8259 number grammar; from_chars alone accepts more.
    std::expected<Value, ParseError> number() {
        const auto start = pos_;
        const auto digits = [this] {
            const auto from = pos_;
            while (ascii::is_digit(peek()))
                ++pos_;
            return pos_ - from;
        };
        consume("-");
        if (peek() == '0')
            ++pos_;
        else if (digits() == 0)
            return fail("expected a digit");
        if (consume(".") && digits() == 0)
            return fail("expected a digit after '.'");
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-')
                ++pos_;
            if (digits() == 0)
                return fail("expected an exponent");
        }
        const auto d = to_double(text_.substr(start, pos_ - start));
        if (!d)
            return fail("number out of range");
        return Value(*d);
    }

    std::optional<unsigned> hex4() {
        if (text_.size() - pos_ < 4)
            return std::nullopt;
        unsigned v = 0;
        const auto [end, ec] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, v, 16);
        if (ec != std::errc{} || end != text_.data() + pos_ + 4)
            return std::nullopt;
        pos_ += 4;
        return v;
    }

    std::expected<char32_t, ParseError> unicode_escape() {
        const auto high = hex4();
        if (!high)
            return fail("expected four hex digits");
        if (*high >= 0xdc00 && *high <= 0xdfff)
            return fail("unpaired surrogate");
        if (*high < 0xd800 || *high > 0xdbff)
            return static_cast<char32_t>(*high);
        if (!consume("\\u"))
            return fail("unpaired surrogate");
        const auto low = hex4();
        if (!low || *low < 0xdc00 || *low > 0xdfff)
            return fail("unpaired surrogate");
        return static_cast<char32_t>(0x10000 + ((*high - 0xd800) << 10) + (*low - 0xdc00));
    }

    std::expected<std::string, ParseError> string() {
        std::string out;
        ++pos_; // opening quote
        for (;;) {
            if (at_end())
                return fail("unterminated string");
            const char c = peek();
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (ascii::is_control(c) && c != 0x7f)
                return fail("control character in a string");
            if (c != '\\') {
                const auto decoded = decode_utf8(text_.substr(pos_));
                if (!decoded)
                    return fail("malformed UTF-8");
                out.append(text_.substr(pos_, decoded->second));
                pos_ += decoded->second;
                continue;
            }
            ++pos_;
            const char e = peek();
            ++pos_;
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out.push_back(e);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                const auto cp = unicode_escape();
                if (!cp)
                    return std::unexpected(cp.error());
                if (*cp == 0)
                    return fail("NUL character in a string");
                append_utf8(out, *cp);
                break;
            }
            default:
                --pos_;
                return fail("unknown escape");
            }
        }
    }
};

void write_string(std::string& out, std::string_view s) {
    out.push_back('"');
    while (!s.empty()) {
        const char c = s.front();
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out.append("\\n");
        } else if (c == '\t') {
            out.append("\\t");
        } else if (ascii::is_control(c)) {
            out.append(std::format("\\u{:04x}", static_cast<unsigned>(c)));
        } else if (static_cast<unsigned char>(c) < 0x80) {
            out.push_back(c);
        } else {
            // Strings come from parse or from checked settings, so they
            // are UTF-8; a stray byte is written as U+FFFD.
            const auto decoded = decode_utf8(s);
            const char32_t cp = decoded ? decoded->first : char32_t{0xfffd};
            if (cp >= 0x10000) {
                const auto v = cp - 0x10000;
                out.append(
                    std::format("\\u{:04x}\\u{:04x}", 0xd800 + (v >> 10), 0xdc00 + (v & 0x3ff)));
            } else {
                out.append(std::format("\\u{:04x}", static_cast<unsigned>(cp)));
            }
            s.remove_prefix(decoded ? decoded->second : 1);
            continue;
        }
        s.remove_prefix(1);
    }
    out.push_back('"');
}

void write_value(std::string& out, const Value& v, int indent) {
    const auto newline = [&out](int level) {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    };
    if (v.is_null()) {
        out.append("null");
    } else if (const auto b = v.as_bool()) {
        out.append(*b ? "true" : "false");
    } else if (const auto i = v.as_integer()) {
        out.append(std::format("{}", *i));
    } else if (const auto d = v.as_number()) {
        out.append(std::format("{}", *d));
    } else if (const auto s = v.as_string()) {
        write_string(out, *s);
    } else if (const auto* a = v.as_array()) {
        if (a->empty()) {
            out.append("[]");
            return;
        }
        out.push_back('[');
        for (std::size_t n = 0; n < a->size(); ++n) {
            newline(indent + 1);
            write_value(out, (*a)[n], indent + 1);
            if (n + 1 < a->size())
                out.push_back(',');
        }
        newline(indent);
        out.push_back(']');
    } else if (const auto* o = v.as_object()) {
        if (o->empty()) {
            out.append("{}");
            return;
        }
        out.push_back('{');
        for (std::size_t n = 0; n < o->size(); ++n) {
            newline(indent + 1);
            write_string(out, (*o)[n].key);
            out.append(": ");
            write_value(out, (*o)[n].value, indent + 1);
            if (n + 1 < o->size())
                out.push_back(',');
        }
        newline(indent);
        out.push_back('}');
    }
}

} // namespace

std::expected<Value, ParseError> parse(std::string_view text) {
    return Parser(text).document();
}

std::string write(const Value& value) {
    std::string out;
    write_value(out, value, 0);
    out.push_back('\n');
    return out;
}

} // namespace pixterm::json
