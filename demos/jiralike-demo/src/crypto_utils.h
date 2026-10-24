#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace utils {

// Standalone SHA-256 implementation (FIPS 180-4)
inline std::string sha256_hex(const std::string &input) {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    auto rotr = [](uint32_t x, uint32_t n) noexcept -> uint32_t {
        return (x >> n) | (x << (32 - n));
    };

    uint32_t h0 = 0x6a09e667;
    uint32_t h1 = 0xbb67ae85;
    uint32_t h2 = 0x3c6ef372;
    uint32_t h3 = 0xa54ff53a;
    uint32_t h4 = 0x510e527f;
    uint32_t h5 = 0x9b05688c;
    uint32_t h6 = 0x1f83d9ab;
    uint32_t h7 = 0x5be0cd19;

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[offset + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[offset + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h0 += a; h1 += b; h2 += c; h3 += d;
        h4 += e; h5 += f; h6 += g; h7 += h;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << h0 << std::setw(8) << h1
        << std::setw(8) << h2 << std::setw(8) << h3
        << std::setw(8) << h4 << std::setw(8) << h5
        << std::setw(8) << h6 << std::setw(8) << h7;
    return oss.str();
}

inline std::string generate_random_hex(size_t bytes = 24) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes; ++i) {
        oss << std::setw(2) << dist(gen);
    }
    return oss.str();
}

inline std::string hash_password(const std::string &password) {
    std::string salt = generate_random_hex(8);
    return salt + "$" + sha256_hex(salt + ":" + password);
}

inline bool verify_password(const std::string &password, const std::string &stored_hash) {
    auto pos = stored_hash.find('$');
    if (pos == std::string::npos) {
        return sha256_hex(password) == stored_hash;
    }
    std::string salt = stored_hash.substr(0, pos);
    std::string expected = stored_hash.substr(pos + 1);
    return sha256_hex(salt + ":" + password) == expected;
}

inline std::string sanitize_filename(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '.') {
        out.erase(out.begin());
    }
    if (out.empty()) {
        out = "attachment.bin";
    }
    if (out.size() > 80) {
        out = out.substr(out.size() - 80);
    }
    return out;
}

inline std::string base64_decode(const std::string &in) {
    static constexpr int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::string out;
    out.reserve((in.size() * 3) / 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

struct multipart_file {
    std::string filename;
    std::string mime_type;
    std::string data;
    bool valid = false;
};

inline multipart_file parse_multipart_file(const std::string &content_type, const std::string &body) {
    multipart_file result;
    auto bpos = content_type.find("boundary=");
    if (bpos == std::string::npos) return result;

    std::string boundary = content_type.substr(bpos + 9);
    if (!boundary.empty() && boundary.front() == '"') {
        boundary.erase(0, 1);
        auto qpos = boundary.find('"');
        if (qpos != std::string::npos) boundary = boundary.substr(0, qpos);
    } else {
        auto spos = boundary.find(';');
        if (spos != std::string::npos) boundary = boundary.substr(0, spos);
    }

    std::string delimiter = "--" + boundary;
    auto start = body.find(delimiter);
    while (start != std::string::npos) {
        start += delimiter.size();
        if (start + 2 <= body.size() && body.compare(start, 2, "--") == 0) break;
        if (start + 2 <= body.size() && body.compare(start, 2, "\r\n") == 0) start += 2;

        auto header_end = body.find("\r\n\r\n", start);
        if (header_end == std::string::npos) break;

        std::string part_headers = body.substr(start, header_end - start);
        auto data_start = header_end + 4;
        auto next_delim = body.find("\r\n" + delimiter, data_start);
        if (next_delim == std::string::npos) break;

        auto fn_pos = part_headers.find("filename=\"");
        if (fn_pos != std::string::npos) {
            fn_pos += 10;
            auto fn_end = part_headers.find('"', fn_pos);
            if (fn_end != std::string::npos) {
                result.filename = part_headers.substr(fn_pos, fn_end - fn_pos);
            }
            result.mime_type = "application/octet-stream";
            auto ct_pos = part_headers.find("Content-Type:");
            if (ct_pos == std::string::npos) ct_pos = part_headers.find("content-type:");
            if (ct_pos != std::string::npos) {
                ct_pos += 13;
                while (ct_pos < part_headers.size() && part_headers[ct_pos] == ' ') ++ct_pos;
                auto ct_end = part_headers.find("\r\n", ct_pos);
                result.mime_type = part_headers.substr(ct_pos, ct_end - ct_pos);
            }
            result.data = body.substr(data_start, next_delim - data_start);
            result.valid = true;
            return result;
        }

        start = next_delim + 2;
    }

    return result;
}

} // namespace utils
