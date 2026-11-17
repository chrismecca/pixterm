// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// A strict JSON reader and writer for the settings file.
///
/// The reader accepts only RFC 8259 JSON, and also rejects what a
/// settings file must never hold: duplicate keys, NUL characters,
/// malformed UTF-8 and deep nesting. The writer output is plain ASCII
/// and reads back to the same value.

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pixterm::json {

class Value;
struct Member;

using Array = std::vector<Value>;
/// Object members, in the order they were read or added.
using Object = std::vector<Member>;

/// A JSON value: null, a boolean, a number, a string, an array or an
/// object. A default-constructed value is null.
class Value {
public:
    // The constructors are defined below, where Member is complete.
    Value();
    Value(std::nullptr_t);
    Value(bool b);
    Value(double d);
    template <std::integral I>
        requires(!std::same_as<I, bool>)
    Value(I i);
    Value(const char* s);
    Value(std::string_view s);
    Value(std::string s);
    Value(Array a);
    Value(Object o);

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::nullptr_t>(data_);
    }
    [[nodiscard]] std::optional<bool> as_bool() const noexcept;
    [[nodiscard]] std::optional<double> as_number() const noexcept;
    /// The number, if it is a whole number that fits in 53 bits.
    [[nodiscard]] std::optional<std::int64_t> as_integer() const noexcept;
    /// The string. The view lives as long as this value.
    [[nodiscard]] std::optional<std::string_view> as_string() const noexcept;
    [[nodiscard]] const Array* as_array() const noexcept;
    [[nodiscard]] const Object* as_object() const noexcept;

    /// The member named key, or null if this is not an object or has
    /// no such member.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;

    friend bool operator==(const Value& a, const Value& b);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

/// One member of an object.
struct Member {
    std::string key;
    Value value;

    friend bool operator==(const Member&, const Member&) = default;
};

inline Value::Value() = default;
inline Value::Value(std::nullptr_t) {}
inline Value::Value(bool b) : data_(b) {}
inline Value::Value(double d) : data_(d) {}
template <std::integral I>
    requires(!std::same_as<I, bool>)
inline Value::Value(I i) : data_(static_cast<double>(i)) {}
inline Value::Value(const char* s) : data_(std::string(s)) {}
inline Value::Value(std::string_view s) : data_(std::string(s)) {}
inline Value::Value(std::string s) : data_(std::move(s)) {}
inline Value::Value(Array a) : data_(std::move(a)) {}
inline Value::Value(Object o) : data_(std::move(o)) {}
inline bool operator==(const Value& a, const Value& b) {
    return a.data_ == b.data_;
}

/// Why text is not acceptable JSON.
struct ParseError {
    std::size_t offset; ///< byte offset of the problem in the text
    std::string message;
};

/// The deepest nesting of arrays and objects parse accepts.
inline constexpr int max_depth = 32;

/// Reads one JSON value, with optional white space around it.
[[nodiscard]] std::expected<Value, ParseError> parse(std::string_view text);

/// Writes a value as indented JSON with a final newline. Characters
/// outside ASCII are written as \\u escapes.
[[nodiscard]] std::string write(const Value& value);

} // namespace pixterm::json
