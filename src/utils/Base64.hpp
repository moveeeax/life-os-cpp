/**
 * @file Base64.hpp
 * @brief base64url encode / decode (RFC 4648 §5, no padding), and standard
 *        base64 encode / decode (RFC 4648 §4, with padding).
 *
 * base64url is used by Auth (JWT), Tokens (signed timed link tokens), and any
 * future primitive that needs to round-trip bytes through a URL-safe channel.
 * Standard base64 is used by the Mi Fitness protocol and stored Mi tokens,
 * HTTP Basic credentials and the QR image of the Mi link. Centralized here so
 * multiple modules don't carry near-identical copies.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Utils::Base64 {

inline std::string url_encode(std::string_view in) {
    static constexpr const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((in.size() * 4) / 3 + 4);
    int bits = 0;
    std::uint32_t buf = 0;
    for (unsigned char c : in) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(alpha[(buf >> bits) & 0x3F]);
        }
    }
    if (bits > 0)
        out.push_back(alpha[(buf << (6 - bits)) & 0x3F]);
    return out;
}

inline std::string url_decode(std::string_view in) {
    static const auto table = [] {
        std::array<std::int8_t, 256> t{};
        t.fill(-1);
        const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int i = 0; i < 64; ++i)
            t[static_cast<unsigned char>(alpha[i])] = static_cast<std::int8_t>(i);
        return t;
    }();

    std::string out;
    out.reserve((in.size() * 3) / 4 + 2);
    std::uint32_t buf = 0;
    int bits = 0;
    std::size_t ndigits = 0;  // significant (non-padding/whitespace) chars
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r')
            continue;
        int v = table[static_cast<unsigned char>(c)];
        if (v < 0)
            throw std::runtime_error("invalid base64url");
        ++ndigits;
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    // Reject non-canonical encodings that decode to the same bytes as a valid
    // token — otherwise a different token *string* round-trips to an identical
    // HMAC/signature and slips past one-shot/JTI replay guards (token
    // malleability). A length of 4k+1 chars is structurally impossible, and the
    // trailing `bits` partial group must be zero-padded by the encoder.
    if (ndigits % 4 == 1)
        throw std::runtime_error("invalid base64url length");
    if (bits > 0 && (buf & ((1U << bits) - 1)) != 0)
        throw std::runtime_error("invalid base64url padding bits");
    return out;
}

/// Standard base64 (RFC 4648 §4) with '=' padding.
inline std::string encode(std::string_view raw) {
    static constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto byte_at = [&](std::size_t i) { return static_cast<unsigned int>(static_cast<unsigned char>(raw[i])); };
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= raw.size()) {
        const unsigned int triple = (byte_at(i) << 16) | (byte_at(i + 1) << 8) | byte_at(i + 2);
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3f]);
        out.push_back(kAlphabet[triple & 0x3f]);
        i += 3;
    }

    const std::size_t rest = raw.size() - i;
    if (rest == 1) {
        const unsigned int triple = byte_at(i) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.append("==");
    } else if (rest == 2) {
        const unsigned int triple = (byte_at(i) << 16) | (byte_at(i + 1) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

/**
 * @brief Strict standard base64 (RFC 4648 §4): the length is a multiple of
 *        four, '=' appears only as the final one or two characters, and every
 *        other character is from the standard alphabet. No whitespace.
 * @throws std::runtime_error with a "base64: ..." message on anything else.
 */
inline std::string decode(std::string_view encoded) {
    static const auto reverse = [] {
        std::array<unsigned char, 256> t{};
        t.fill(0xff);  // 0xff: not a base64 character
        const std::string_view alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t i = 0; i < alpha.size(); ++i)
            t[static_cast<unsigned char>(alpha[i])] = static_cast<unsigned char>(i);
        return t;
    }();

    if (encoded.size() % 4 != 0)
        throw std::runtime_error("base64: length is not a multiple of four");
    if (encoded.empty())
        return {};

    std::size_t padding = 0;
    if (encoded[encoded.size() - 1] == '=') {
        ++padding;
        if (encoded.size() >= 2 && encoded[encoded.size() - 2] == '=')
            ++padding;
    }

    std::string out;
    out.reserve((encoded.size() / 4) * 3);

    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        unsigned int quad = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const std::size_t at = i + k;
            const bool is_pad = encoded[at] == '=';
            // Padding is allowed only at the very end: "=" in the middle is garbage.
            if (is_pad && at + padding < encoded.size())
                throw std::runtime_error("base64: padding in the middle of the input");
            const unsigned char value = is_pad ? 0 : reverse[static_cast<unsigned char>(encoded[at])];
            if (value == 0xff)
                throw std::runtime_error("base64: character outside the standard alphabet");
            quad = (quad << 6) | value;
        }
        out.push_back(static_cast<char>((quad >> 16) & 0xff));
        out.push_back(static_cast<char>((quad >> 8) & 0xff));
        out.push_back(static_cast<char>(quad & 0xff));
    }

    // std::min instead of a plain subtraction: GCC under -Werror=stringop-overflow
    // cannot prove that padding never exceeds the size and assumes a memset of
    // SIZE_MAX is possible. The clamp makes the result provably non-negative.
    out.resize(out.size() - std::min(padding, out.size()));
    return out;
}

}  // namespace Utils::Base64
