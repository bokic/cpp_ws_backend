#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <chrono>
#include <stdexcept>
#include <type_traits>

namespace backend::db {

class db_exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class row {
public:
    virtual ~row() = default;

    virtual size_t column_count() const noexcept = 0;
    virtual std::string_view column_name(size_t index) const = 0;
    virtual size_t column_index(std::string_view name) const = 0;
    virtual bool is_null(size_t index) const = 0;
    virtual bool is_null(std::string_view name) const { return is_null(column_index(name)); }

    virtual int32_t get_int32(size_t index) const = 0;
    virtual int64_t get_int64(size_t index) const = 0;
    virtual double get_double(size_t index) const = 0;
    virtual std::string get_string(size_t index) const = 0;
    virtual std::span<const uint8_t> get_blob(size_t index) const = 0;
    virtual std::chrono::system_clock::time_point get_timestamp(size_t index) const = 0;

    template <typename T>
    T get(size_t index) const {
        using Decayed = std::decay_t<T>;
        if constexpr (std::is_same_v<Decayed, bool>) {
            return get_int32(index) != 0;
        } else if constexpr (std::is_same_v<Decayed, int> || std::is_same_v<Decayed, int32_t>) {
            return get_int32(index);
        } else if constexpr (std::is_same_v<Decayed, int64_t> || std::is_same_v<Decayed, long long>) {
            return get_int64(index);
        } else if constexpr (std::is_floating_point_v<Decayed>) {
            return static_cast<T>(get_double(index));
        } else if constexpr (std::is_same_v<Decayed, std::string>) {
            return get_string(index);
        } else if constexpr (std::is_same_v<Decayed, std::span<const uint8_t>>) {
            return get_blob(index);
        } else if constexpr (std::is_same_v<Decayed, std::vector<uint8_t>>) {
            auto b = get_blob(index);
            return std::vector<uint8_t>(b.begin(), b.end());
        } else if constexpr (std::is_same_v<Decayed, std::chrono::system_clock::time_point>) {
            return get_timestamp(index);
        } else {
            static_assert(!sizeof(T), "Unsupported type for row::get<T>()");
        }
    }

    template <typename T>
    T get(std::string_view name) const {
        return get<T>(column_index(name));
    }

    template <typename T>
    std::optional<T> get_optional(size_t index) const {
        if (is_null(index)) return std::nullopt;
        return get<T>(index);
    }

    template <typename T>
    std::optional<T> get_optional(std::string_view name) const {
        return get_optional<T>(column_index(name));
    }
};

class result_set {
public:
    virtual ~result_set() = default;

    virtual bool next() = 0;
    virtual const row& current_row() const = 0;
    virtual size_t column_count() const noexcept = 0;
    virtual std::string_view column_name(size_t index) const = 0;
    virtual size_t column_index(std::string_view name) const = 0;
    virtual uint64_t affected_rows() const noexcept { return 0; }
};

class statement {
public:
    virtual ~statement() = default;

    // Positional binding (1-based index)
    virtual void bind(size_t index, std::nullptr_t) = 0;
    virtual void bind(size_t index, int32_t val) = 0;
    virtual void bind(size_t index, int64_t val) = 0;
    virtual void bind(size_t index, double val) = 0;
    virtual void bind(size_t index, std::string_view val) = 0;
    virtual void bind(size_t index, std::span<const uint8_t> val) = 0;
    virtual void bind(size_t index, std::chrono::system_clock::time_point val) = 0;

    // Named binding
    virtual void bind(std::string_view name, std::nullptr_t) = 0;
    virtual void bind(std::string_view name, int32_t val) = 0;
    virtual void bind(std::string_view name, int64_t val) = 0;
    virtual void bind(std::string_view name, double val) = 0;
    virtual void bind(std::string_view name, std::string_view val) = 0;
    virtual void bind(std::string_view name, std::span<const uint8_t> val) = 0;
    virtual void bind(std::string_view name, std::chrono::system_clock::time_point val) = 0;

    virtual void execute() = 0;
    virtual std::unique_ptr<result_set> query() = 0;
    virtual void reset() = 0;

    template <typename... Args>
    void bind_all(Args&&... args) {
        size_t idx = 1;
        ([&] {
            bind_arg(idx++, std::forward<Args>(args));
        }(), ...);
    }

private:
    template <typename T>
    void bind_arg(size_t idx, const T& arg) {
        using Decayed = std::decay_t<T>;
        if constexpr (std::is_same_v<Decayed, std::nullptr_t>) {
            bind(idx, nullptr);
        } else if constexpr (std::is_same_v<Decayed, bool>) {
            bind(idx, static_cast<int32_t>(arg ? 1 : 0));
        } else if constexpr (std::is_integral_v<Decayed>) {
            if constexpr (sizeof(Decayed) <= 4) {
                bind(idx, static_cast<int32_t>(arg));
            } else {
                bind(idx, static_cast<int64_t>(arg));
            }
        } else if constexpr (std::is_floating_point_v<Decayed>) {
            bind(idx, static_cast<double>(arg));
        } else if constexpr (std::is_convertible_v<T, std::string_view>) {
            bind(idx, std::string_view(arg));
        } else if constexpr (std::is_same_v<Decayed, std::span<const uint8_t>>) {
            bind(idx, arg);
        } else if constexpr (std::is_same_v<Decayed, std::vector<uint8_t>>) {
            bind(idx, std::span<const uint8_t>(arg.data(), arg.size()));
        } else if constexpr (std::is_same_v<Decayed, std::chrono::system_clock::time_point>) {
            bind(idx, arg);
        } else {
            static_assert(!sizeof(T), "Unsupported type for statement parameter binding");
        }
    }
};

class transaction {
public:
    virtual ~transaction() = default;

    virtual void commit() = 0;
    virtual void rollback() = 0;
    virtual bool is_active() const noexcept = 0;
};

class database {
public:
    virtual ~database() = default;

    virtual void connect(const std::string &conn_str) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const noexcept = 0;
    virtual bool ping() noexcept = 0;

    virtual void execute(std::string_view sql) = 0;
    virtual std::unique_ptr<statement> prepare(std::string_view sql) = 0;
    virtual std::unique_ptr<transaction> begin_transaction() = 0;

    template <typename... Args>
    void execute(std::string_view sql, Args&&... args) {
        auto stmt = prepare(sql);
        stmt->bind_all(std::forward<Args>(args)...);
        stmt->execute();
    }

    template <typename... Args>
    std::unique_ptr<result_set> query(std::string_view sql, Args&&... args) {
        auto stmt = prepare(sql);
        stmt->bind_all(std::forward<Args>(args)...);
        return stmt->query();
    }
};

} // namespace backend::db

namespace db {
    using namespace backend::db;
}
