// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include <pixterm/xdmcp.hpp>

namespace pixterm::xdmcp {

namespace {

constexpr unsigned version = 1;
constexpr std::size_t header_length = 6;

// Opcodes.
constexpr unsigned broadcast_query = 1;
constexpr unsigned query = 2;
constexpr unsigned indirect_query = 3;
constexpr unsigned willing = 5;
constexpr unsigned unwilling = 6;

// Reads big-endian fields from a datagram, failing past its end.
class Reader {
public:
    explicit Reader(std::span<const unsigned char> data) : data_(data) {}

    std::optional<unsigned> card16() {
        if (data_.size() < 2)
            return std::nullopt;
        const unsigned v = (static_cast<unsigned>(data_[0]) << 8U) | data_[1];
        data_ = data_.subspan(2);
        return v;
    }

    // An ARRAY8, as cleaned text.
    std::optional<std::string> array8() {
        const auto n = card16();
        if (!n || data_.size() < *n)
            return std::nullopt;
        std::string text;
        for (const auto c : data_.first(*n)) {
            if (text.size() == max_text)
                break;
            text.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?');
        }
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        data_ = data_.subspan(*n);
        return text;
    }

    [[nodiscard]] bool at_end() const { return data_.empty(); }

private:
    std::span<const unsigned char> data_;
};

} // namespace

std::vector<unsigned char> encode_query(QueryKind kind) {
    unsigned op = query;
    if (kind == QueryKind::broadcast)
        op = broadcast_query;
    else if (kind == QueryKind::indirect)
        op = indirect_query;
    // Header (version, opcode, length 1), then an empty list of
    // authentication names.
    return {0, version, 0, static_cast<unsigned char>(op), 0, 1, 0};
}

std::optional<Reply> parse_reply(std::span<const unsigned char> datagram) {
    Reader header(datagram.first(std::min(datagram.size(), header_length)));
    const auto v = header.card16();
    const auto op = header.card16();
    const auto length = header.card16();
    if (!v || !op || !length || *v != version || *length != datagram.size() - header_length)
        return std::nullopt;

    Reader body(datagram.subspan(header_length));
    Reply r;
    if (*op == willing) {
        r.willing = true;
        if (!body.array8()) // authentication name
            return std::nullopt;
    } else if (*op != unwilling) {
        return std::nullopt;
    }
    auto hostname = body.array8();
    auto status = body.array8();
    if (!hostname || !status || !body.at_end())
        return std::nullopt;
    r.hostname = std::move(*hostname);
    r.status = std::move(*status);
    return r;
}

} // namespace pixterm::xdmcp
