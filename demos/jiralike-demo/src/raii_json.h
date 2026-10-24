#pragma once

#include <json-c/json.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace json {

class json_value {
public:
    json_value() = default;

    explicit json_value(json_object *obj) noexcept : m_obj(obj) {}

    ~json_value() {
        reset();
    }

    json_value(const json_value &) = delete;
    json_value &operator=(const json_value &) = delete;

    json_value(json_value &&other) noexcept : m_obj(other.m_obj) {
        other.m_obj = nullptr;
    }

    json_value &operator=(json_value &&other) noexcept {
        if (this != &other) {
            reset();
            m_obj = other.m_obj;
            other.m_obj = nullptr;
        }
        return *this;
    }

    void reset() noexcept {
        if (m_obj) {
            json_object_put(m_obj);
            m_obj = nullptr;
        }
    }

    json_object *get() const noexcept { return m_obj; }
    explicit operator bool() const noexcept { return m_obj != nullptr; }

    json_object *release() noexcept {
        json_object *obj = m_obj;
        m_obj = nullptr;
        return obj;
    }

    const char *to_string() const noexcept {
        return m_obj ? json_object_to_json_string_ext(m_obj, JSON_C_TO_STRING_PLAIN) : "{}";
    }

    static json_value parse(const std::string &raw) {
        if (raw.empty()) return json_value(nullptr);
        json_object *parsed = json_tokener_parse(raw.c_str());
        return json_value(parsed);
    }

    static json_value make_object() {
        json_object *obj = json_object_new_object();
        if (!obj) throw std::runtime_error("Failed to allocate JSON object");
        return json_value(obj);
    }

    static json_value make_array() {
        json_object *arr = json_object_new_array();
        if (!arr) throw std::runtime_error("Failed to allocate JSON array");
        return json_value(arr);
    }

    static json_value make_string(const char *str) {
        return json_value(json_object_new_string(str ? str : ""));
    }

    static json_value make_string(const std::string &str) {
        return json_value(json_object_new_string(str.c_str()));
    }

    static json_value make_int(int val) {
        return json_value(json_object_new_int(val));
    }

    static json_value make_int64(int64_t val) {
        return json_value(json_object_new_int64(val));
    }

    static json_value make_bool(bool val) {
        return json_value(json_object_new_boolean(val ? 1 : 0));
    }

    static json_value make_null() {
        return json_value(nullptr);
    }

    void add(const char *key, json_value &&val) {
        if (!m_obj) throw std::runtime_error("Null JSON object");
        int rc = json_object_object_add(m_obj, key, val.get());
        if (rc != 0) {
            throw std::runtime_error("Failed to add key to JSON object");
        }
        val.release();
    }

    void add_str(const char *key, const std::string &val) {
        add(key, make_string(val));
    }

    void add_int(const char *key, int val) {
        add(key, make_int(val));
    }

    void add_int64(const char *key, int64_t val) {
        add(key, make_int64(val));
    }

    void add_bool(const char *key, bool val) {
        add(key, make_bool(val));
    }

    void add_nullable_int(const char *key, int val) {
        if (val <= 0) {
            add(key, make_null());
        } else {
            add(key, make_int(val));
        }
    }

    void add(json_value &&val) {
        if (!m_obj) throw std::runtime_error("Null JSON array");
        int rc = json_object_array_add(m_obj, val.get());
        if (rc != 0) {
            throw std::runtime_error("Failed to add element to JSON array");
        }
        val.release();
    }

    bool has_key(const char *key) const noexcept {
        if (!m_obj || !json_object_is_type(m_obj, json_type_object)) return false;
        json_object *field = nullptr;
        return json_object_object_get_ex(m_obj, key, &field) && field != nullptr;
    }

    std::string get_string(const char *key, const std::string &default_val = "") const {
        if (!m_obj || !json_object_is_type(m_obj, json_type_object)) return default_val;
        json_object *field = nullptr;
        if (!json_object_object_get_ex(m_obj, key, &field) || !field) return default_val;
        const char *s = json_object_get_string(field);
        return s ? std::string(s) : default_val;
    }

    int get_int(const char *key, int default_val = 0) const noexcept {
        if (!m_obj || !json_object_is_type(m_obj, json_type_object)) return default_val;
        json_object *field = nullptr;
        if (!json_object_object_get_ex(m_obj, key, &field) || !field) return default_val;
        if (json_object_is_type(field, json_type_null)) return 0;
        return json_object_get_int(field);
    }

    bool get_bool(const char *key, bool default_val = false) const noexcept {
        if (!m_obj || !json_object_is_type(m_obj, json_type_object)) return default_val;
        json_object *field = nullptr;
        if (!json_object_object_get_ex(m_obj, key, &field) || !field) return default_val;
        return json_object_get_boolean(field) != 0;
    }

private:
    json_object *m_obj = nullptr;
};

} // namespace json
