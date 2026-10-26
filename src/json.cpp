#include "json.h"

#include <cstdlib>

namespace json {

namespace {

const Value kNull;

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    int depth = 0;

    explicit Parser(const std::string& text) : s(text) {}

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool fail(const char* m) {
        if (err.empty()) err = std::string(m) + " at offset " + std::to_string(i);
        return false;
    }
    bool lit(const char* w) {
        size_t n = 0;
        while (w[n]) ++n;
        if (s.compare(i, n, w) != 0) return fail("bad literal");
        i += n;
        return true;
    }

    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }

    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail("bad hex digit");
        }
        return true;
    }

    bool str(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool value(Value& v) {
        if (++depth > 200) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        bool ok = true;
        char c = s[i];
        if (c == '{') {
            v.type = Value::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; }
            else {
                for (;;) {
                    ws();
                    std::string k;
                    if (i >= s.size() || !str(k)) { ok = false; break; }
                    ws();
                    if (i >= s.size() || s[i] != ':') { ok = fail("expected ':'"); break; }
                    ++i;
                    v.obj.emplace_back(std::move(k), Value());
                    if (!value(v.obj.back().second)) { ok = false; break; }
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == '}') { ++i; break; }
                    ok = fail("expected ',' or '}'");
                    break;
                }
            }
        } else if (c == '[') {
            v.type = Value::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; }
            else {
                for (;;) {
                    v.arr.emplace_back();
                    if (!value(v.arr.back())) { ok = false; break; }
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == ']') { ++i; break; }
                    ok = fail("expected ',' or ']'");
                    break;
                }
            }
        } else if (c == '"') {
            v.type = Value::String;
            ok = str(v.str);
        } else if (c == 't') {
            v.type = Value::Bool; v.b = true; ok = lit("true");
        } else if (c == 'f') {
            v.type = Value::Bool; v.b = false; ok = lit("false");
        } else if (c == 'n') {
            v.type = Value::Null; ok = lit("null");
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = s.c_str() + i;
            char* end = nullptr;
            v.type = Value::Number;
            v.num = strtod(start, &end);
            if (end == start) ok = fail("bad number");
            i += end - start;
        } else {
            ok = fail("unexpected character");
        }
        --depth;
        return ok;
    }
};

}  // namespace

const Value& Value::operator[](const std::string& key) const {
    if (type != Object) return kNull;
    for (auto& kv : obj)
        if (kv.first == key) return kv.second;
    return kNull;
}

const Value& Value::operator[](size_t idx) const {
    if (type != Array || idx >= arr.size()) return kNull;
    return arr[idx];
}

bool parse(const std::string& text, Value& out, std::string* err) {
    Parser p(text);
    out = Value();
    bool ok = p.value(out);
    if (ok) {
        p.ws();
        if (p.i != text.size()) ok = p.fail("trailing characters");
    }
    if (!ok && err) *err = p.err;
    return ok;
}

}  // namespace json
