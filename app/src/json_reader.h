#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A minimal JSON reader (the app has none outside cgltf's private one): S12 Task 4
// wrote it for the behaviour graph; Task 8 shares it for the events sidecar.

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;  // document order

    const Json* get(std::string_view key) const {
        for (const auto& [k, v] : object) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

struct JsonReader {
    std::string_view text;
    size_t at = 0;

    void skip() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at;
    }
    [[noreturn]] void error(const char* what) const {
        throw std::runtime_error(std::string("JSON: ") + what + " at byte " + std::to_string(at));
    }
    char peek() {
        skip();
        if (at >= text.size()) error("unexpected end");
        return text[at];
    }
    void expect(char c) {
        if (peek() != c) error((std::string("expected '") + c + "'").c_str());
        ++at;
    }
    bool literal(std::string_view word) {
        if (text.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }

    static void put_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    std::string str() {
        expect('"');
        std::string out;
        while (true) {
            if (at >= text.size()) error("unterminated string");
            const char c = text[at++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= text.size()) error("unterminated escape");
            const char e = text[at++];
            switch (e) {
                case '"': case '\\': case '/': out += e; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (at + 4 > text.size()) error("short \\u escape");
                    // ponytail: a surrogate pair is encoded as two 3-byte units (labels only).
                    const std::string hex4(text.substr(at, 4));
                    put_utf8(out, static_cast<unsigned>(std::strtoul(hex4.c_str(), nullptr, 16)));
                    at += 4;
                    break;
                }
                default: error("bad escape");
            }
        }
    }

    Json value() {
        Json v;
        const char c = peek();
        if (c == '{') {
            v.kind = Json::Object;
            ++at;
            if (peek() == '}') return ++at, v;
            while (true) {
                std::string key = str();
                expect(':');
                v.object.emplace_back(std::move(key), value());
                if (peek() == '}') return ++at, v;
                expect(',');
            }
        }
        if (c == '[') {
            v.kind = Json::Array;
            ++at;
            if (peek() == ']') return ++at, v;
            while (true) {
                v.array.push_back(value());
                if (peek() == ']') return ++at, v;
                expect(',');
            }
        }
        if (c == '"') {
            v.kind = Json::String;
            v.string = str();
            return v;
        }
        if (literal("null")) return v;
        if (literal("true") || literal("false")) {
            v.kind = Json::Bool;
            v.boolean = text[at - 2] == 'u';  // tr[u]e / fal[s]e
            return v;
        }
        const size_t start = at;
        while (at < text.size() && std::string_view("+-0123456789.eE").find(text[at]) != std::string_view::npos) ++at;
        if (at == start) error("unexpected character");
        const std::string token(text.substr(start, at - start));
        char* end = nullptr;
        v.kind = Json::Number;
        v.number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) error("bad number");
        return v;
    }
};

// Parses a whole document; trailing data is an error. Throws std::runtime_error.
inline Json json_parse(std::string_view text) {
    JsonReader reader{text};
    Json doc = reader.value();
    reader.skip();
    if (reader.at != text.size()) reader.error("trailing data");
    return doc;
}
