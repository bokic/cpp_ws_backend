#pragma once

#include "wsworker.h"
#include "raii_json.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace governance {

class token_bucket_rate_limiter {
public:
    struct bucket {
        double tokens = 0.0;
        std::chrono::steady_clock::time_point last_refill;
        std::string entity_type; // "ip" or "api_token"
        std::string identifier;
        uint64_t total_requests = 0;
        uint64_t blocked_requests = 0;
    };

    token_bucket_rate_limiter()
        : m_enabled(true),
          m_ip_capacity(60.0),
          m_ip_refill_rate(1.0),       // 60 requests/minute
          m_token_capacity(180.0),
          m_token_refill_rate(3.0)     // 180 requests/minute
    {}

    bool is_enabled() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_enabled;
    }

    void set_enabled(bool enabled) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = enabled;
    }

    void update_config(bool enabled, double ip_capacity, double ip_refill_rate,
                       double token_capacity, double token_refill_rate) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = enabled;
        if (ip_capacity > 0) m_ip_capacity = ip_capacity;
        if (ip_refill_rate > 0) m_ip_refill_rate = ip_refill_rate;
        if (token_capacity > 0) m_token_capacity = token_capacity;
        if (token_refill_rate > 0) m_token_refill_rate = token_refill_rate;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buckets.clear();
    }

    void reset_key(const std::string &key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buckets.erase(key);
    }

    // Extract client identification from FastCGI environment
    void extract_client_info(const std::map<std::string, std::string> &header,
                             std::string &client_key,
                             std::string &entity_type,
                             std::string &raw_id) const {
        // 1. Check API token / Bearer token
        auto it_auth = header.find("HTTP_AUTHORIZATION");
        if (it_auth != header.end() && !it_auth->second.empty()) {
            const std::string &auth = it_auth->second;
            if (auth.rfind("Bearer ", 0) == 0 || auth.rfind("bearer ", 0) == 0) {
                raw_id = auth.substr(7);
                // trim whitespace
                while (!raw_id.empty() && (raw_id.front() == ' ' || raw_id.front() == '\t')) raw_id.erase(0, 1);
                while (!raw_id.empty() && (raw_id.back() == ' ' || raw_id.back() == '\t')) raw_id.pop_back();
                if (!raw_id.empty()) {
                    entity_type = "api_token";
                    client_key = "token:" + raw_id;
                    return;
                }
            }
        }

        auto it_key = header.find("HTTP_X_API_KEY");
        if (it_key != header.end() && !it_key->second.empty()) {
            raw_id = it_key->second;
            entity_type = "api_token";
            client_key = "token:" + raw_id;
            return;
        }

        // 2. Client IP address
        entity_type = "ip";
        auto it_fwd = header.find("HTTP_X_FORWARDED_FOR");
        if (it_fwd != header.end() && !it_fwd->second.empty()) {
            std::string fwd = it_fwd->second;
            auto comma_pos = fwd.find(',');
            if (comma_pos != std::string::npos) {
                raw_id = fwd.substr(0, comma_pos);
            } else {
                raw_id = fwd;
            }
            while (!raw_id.empty() && (raw_id.front() == ' ' || raw_id.front() == '\t')) raw_id.erase(0, 1);
            while (!raw_id.empty() && (raw_id.back() == ' ' || raw_id.back() == '\t')) raw_id.pop_back();
        }

        if (raw_id.empty()) {
            auto it_real = header.find("HTTP_X_REAL_IP");
            if (it_real != header.end() && !it_real->second.empty()) {
                raw_id = it_real->second;
            }
        }

        if (raw_id.empty()) {
            auto it_remote = header.find("REMOTE_ADDR");
            if (it_remote != header.end() && !it_remote->second.empty()) {
                raw_id = it_remote->second;
            }
        }

        if (raw_id.empty()) {
            raw_id = "127.0.0.1";
        }

        client_key = "ip:" + raw_id;
    }

    bool is_exempt_path(const std::string &path) const {
        if (path.empty()) return false;
        // Non-API routes are exempt
        if (path.rfind("/api/", 0) != 0) return true;

        // Vital internal and documentation endpoints
        if (path == "/api/health" ||
            path == "/api/setup/status" ||
            path == "/api/openapi.json" ||
            path == "/api/docs" ||
            path.rfind("/api/rate-limits", 0) == 0) {
            return true;
        }
        return false;
    }

    // Process an incoming FastCGI request
    // Returns true if allowed, or false if rate limited (in which case 429 response is written)
    bool process(backend::wsworker *worker, const std::map<std::string, std::string> &header) {
        if (!worker) return true;

        std::string path;
        auto it_script = header.find("SCRIPT_NAME");
        if (it_script != header.end() && !it_script->second.empty()) {
            path = it_script->second;
        } else {
            auto it_uri = header.find("REQUEST_URI");
            if (it_uri != header.end()) {
                auto qpos = it_uri->second.find('?');
                path = (qpos == std::string::npos) ? it_uri->second : it_uri->second.substr(0, qpos);
            }
        }

        // Bypass check
        if (is_exempt_path(path)) {
            return true;
        }

        // Check explicit test bypass header if provided
        auto it_bypass = header.find("HTTP_X_RATE_LIMIT_BYPASS");
        if (it_bypass != header.end() && it_bypass->second == "true") {
            return true;
        }

        std::string client_key;
        std::string entity_type;
        std::string raw_id;
        extract_client_info(header, client_key, entity_type, raw_id);

        double capacity = 0.0;
        double refill_rate = 0.0;
        int retry_after = 0;
        int remaining = 0;
        int64_t reset_epoch = 0;
        bool allowed = false;

        auto now = std::chrono::steady_clock::now();
        auto wall_now = std::time(nullptr);

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (!m_enabled) {
                return true;
            }

            if (entity_type == "api_token") {
                capacity = m_token_capacity;
                refill_rate = m_token_refill_rate;
            } else {
                capacity = m_ip_capacity;
                refill_rate = m_ip_refill_rate;
            }

            auto it = m_buckets.find(client_key);
            if (it == m_buckets.end()) {
                bucket b;
                b.tokens = capacity;
                b.last_refill = now;
                b.entity_type = entity_type;
                b.identifier = raw_id;
                b.total_requests = 1;
                b.blocked_requests = 0;

                // Consume 1 token for this request
                b.tokens -= 1.0;
                allowed = true;
                remaining = static_cast<int>(std::floor(b.tokens));
                reset_epoch = static_cast<int64_t>(wall_now) +
                              static_cast<int64_t>(std::ceil((capacity - b.tokens) / refill_rate));

                m_buckets.emplace(client_key, std::move(b));
            } else {
                bucket &b = it->second;
                double elapsed = std::chrono::duration<double>(now - b.last_refill).count();
                b.tokens = std::min(capacity, b.tokens + elapsed * refill_rate);
                b.last_refill = now;
                b.total_requests++;

                if (b.tokens >= 1.0) {
                    b.tokens -= 1.0;
                    allowed = true;
                    remaining = static_cast<int>(std::floor(b.tokens));
                    reset_epoch = static_cast<int64_t>(wall_now) +
                                  static_cast<int64_t>(std::ceil((capacity - b.tokens) / refill_rate));
                } else {
                    b.blocked_requests++;
                    allowed = false;
                    remaining = 0;
                    retry_after = std::max(1, static_cast<int>(std::ceil((1.0 - b.tokens) / refill_rate)));
                    reset_epoch = static_cast<int64_t>(wall_now) + retry_after;
                }
            }

            // Periodic cleanup if map grows too large
            if (m_buckets.size() > 5000) {
                clean_stale_buckets(now);
            }
        }

        if (allowed) {
            return true;
        }

        // Reject with HTTP 429 Too Many Requests
        std::string proto = "HTTP/1.1";
        auto it_proto = header.find("SERVER_PROTOCOL");
        if (it_proto != header.end() && !it_proto->second.empty()) {
            proto = it_proto->second;
        }

        auto err_json = json::json_value::make_object();
        err_json.add_str("error", "Too Many Requests");
        err_json.add_str("message", "Rate limit exceeded. Please retry after " + std::to_string(retry_after) + " seconds.");
        err_json.add_int("retry_after", retry_after);
        err_json.add_int("limit", static_cast<int>(capacity));
        err_json.add_int("remaining", 0);
        err_json.add_int("reset", reset_epoch);
        err_json.add_str("client_type", entity_type);

        const char *payload_str = err_json.to_string();
        int payload_len = static_cast<int>(std::strlen(payload_str));

        FCGX_FPrintF(worker->out(),
                     "%s 429 Too Many Requests\r\n"
                     "Status: 429 Too Many Requests\r\n"
                     "Retry-After: %d\r\n"
                     "X-RateLimit-Limit: %d\r\n"
                     "X-RateLimit-Remaining: 0\r\n"
                     "X-RateLimit-Reset: %ld\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "Cache-Control: no-store\r\n"
                     "Content-Length: %d\r\n\r\n%s",
                     proto.c_str(), retry_after, static_cast<int>(capacity), reset_epoch, payload_len, payload_str);

        return false;
    }

    json::json_value get_status(const std::map<std::string, std::string> &header) {
        std::string client_key;
        std::string entity_type;
        std::string raw_id;
        extract_client_info(header, client_key, entity_type, raw_id);

        auto now = std::chrono::steady_clock::now();
        auto wall_now = std::time(nullptr);

        std::lock_guard<std::mutex> lock(m_mutex);

        double capacity = (entity_type == "api_token") ? m_token_capacity : m_ip_capacity;
        double refill_rate = (entity_type == "api_token") ? m_token_refill_rate : m_ip_refill_rate;

        double current_tokens = capacity;
        uint64_t total_reqs = 0;
        uint64_t blocked_reqs = 0;

        auto it = m_buckets.find(client_key);
        if (it != m_buckets.end()) {
            double elapsed = std::chrono::duration<double>(now - it->second.last_refill).count();
            current_tokens = std::min(capacity, it->second.tokens + elapsed * refill_rate);
            total_reqs = it->second.total_requests;
            blocked_reqs = it->second.blocked_requests;
        }

        int remaining = std::max(0, static_cast<int>(std::floor(current_tokens)));
        int64_t reset_epoch = static_cast<int64_t>(wall_now) +
                              static_cast<int64_t>(std::ceil((capacity - current_tokens) / refill_rate));

        auto res = json::json_value::make_object();
        res.add_bool("enabled", m_enabled);
        res.add_str("client_key", client_key);
        res.add_str("entity_type", entity_type);
        res.add_str("identifier", raw_id);
        res.add_int("limit", static_cast<int>(capacity));
        res.add_int("remaining", remaining);
        res.add_double("tokens", current_tokens);
        res.add_int("reset", reset_epoch);
        res.add_double("refill_rate_per_sec", refill_rate);
        res.add_int("client_total_requests", total_reqs);
        res.add_int("client_blocked_requests", blocked_reqs);
        res.add_int("active_tracked_buckets", static_cast<int64_t>(m_buckets.size()));

        auto limits = json::json_value::make_object();
        limits.add_int("ip_capacity", static_cast<int>(m_ip_capacity));
        limits.add_double("ip_refill_rate", m_ip_refill_rate);
        limits.add_int("token_capacity", static_cast<int>(m_token_capacity));
        limits.add_double("token_refill_rate", m_token_refill_rate);
        res.add("limits", std::move(limits));

        return res;
    }

private:
    void clean_stale_buckets(const std::chrono::steady_clock::time_point &now) {
        // Evict buckets not seen for > 1 hour
        for (auto it = m_buckets.begin(); it != m_buckets.end(); ) {
            double elapsed = std::chrono::duration<double>(now - it->second.last_refill).count();
            if (elapsed > 3600.0) {
                it = m_buckets.erase(it);
            } else {
                ++it;
            }
        }
    }

    mutable std::mutex m_mutex;
    bool m_enabled;
    double m_ip_capacity;
    double m_ip_refill_rate;
    double m_token_capacity;
    double m_token_refill_rate;
    std::unordered_map<std::string, bucket> m_buckets;
};

} // namespace governance
