#pragma once
// Minimal JSON reader — just enough for the weather.gov API.
#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    // Missing keys / indices / wrong types yield a shared null value, so lookups can be chained.
    const Value& operator[](const std::string& key) const;
    const Value& operator[](size_t i) const;
    bool isNull() const { return type == Null; }
    bool isNumber() const { return type == Number; }
    double number(double def = 0) const { return type == Number ? num : def; }
    std::string string(const std::string& def = "") const { return type == String ? str : def; }
    size_t size() const { return type == Array ? arr.size() : type == Object ? obj.size() : 0; }
};

bool parse(const std::string& text, Value& out, std::string* err = nullptr);

}  // namespace json
