#pragma once

#include <json-c/json.h>
#include <string>
#include <stdexcept>
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
        return m_obj ? json_object_get_string(m_obj) : "";
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

    void add(const char *key, json_value &&val) {
        if (!m_obj) throw std::runtime_error("Null JSON object");
        int rc = json_object_object_add(m_obj, key, val.get());
        if (rc != 0) {
            throw std::runtime_error("Failed to add key to JSON object");
        }
        val.release();
    }

    void add(json_value &&val) {
        if (!m_obj) throw std::runtime_error("Null JSON array");
        int rc = json_object_array_add(m_obj, val.get());
        if (rc != 0) {
            throw std::runtime_error("Failed to add element to JSON array");
        }
        val.release();
    }

private:
    json_object *m_obj = nullptr;
};

} // namespace json
