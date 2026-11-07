#pragma once

#include "raii_json.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace realtime {

inline std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

struct LiveEvent {
    uint64_t id = 0;
    std::string event_type;
    std::string created_at;
    int actor_user_id = 0;
    std::string actor_name;
    int item_id = 0;
    std::string item_key;
    std::string payload_json;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("id", static_cast<int>(id));
        obj.add_str("event_type", event_type);
        obj.add_str("created_at", created_at);
        obj.add_int("actor_user_id", actor_user_id);
        obj.add_str("actor_name", actor_name);
        obj.add_int("item_id", item_id);
        obj.add_str("item_key", item_key);
        if (!payload_json.empty()) {
            json::json_value parsed = json::json_value::parse(payload_json);
            if (parsed) {
                obj.add("payload", std::move(parsed));
            } else {
                obj.add_str("payload", payload_json);
            }
        }
        return obj;
    }
};

struct PresenceUser {
    int user_id = 0;
    std::string username;
    std::string display_name;
    std::string avatar_color;
    int item_id = 0;
    std::string item_key;
    std::string action; // "viewing", "editing", "typing"
    std::chrono::steady_clock::time_point last_seen;

    json::json_value to_json() const {
        auto obj = json::json_value::make_object();
        obj.add_int("user_id", user_id);
        obj.add_str("username", username);
        obj.add_str("display_name", display_name);
        obj.add_str("avatar_color", avatar_color);
        obj.add_int("item_id", item_id);
        obj.add_str("item_key", item_key);
        obj.add_str("action", action);
        return obj;
    }
};

class RealtimeEngine {
public:
    static RealtimeEngine& instance() {
        static RealtimeEngine eng;
        return eng;
    }

    uint64_t publish(const std::string& event_type,
                     const std::string& payload_json,
                     int actor_user_id = 0,
                     const std::string& actor_name = "",
                     int item_id = 0,
                     const std::string& item_key = "") {
        std::unique_lock<std::mutex> lock(m_mutex);
        uint64_t eid = m_next_id++;
        LiveEvent ev;
        ev.id = eid;
        ev.event_type = event_type;
        ev.created_at = current_iso8601();
        ev.actor_user_id = actor_user_id;
        ev.actor_name = actor_name;
        ev.item_id = item_id;
        ev.item_key = item_key;
        ev.payload_json = payload_json;

        m_events.push_back(ev);
        while (m_events.size() > 500) {
            m_events.pop_front();
        }
        lock.unlock();
        m_cv.notify_all();
        return eid;
    }

    uint64_t latest_id() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_next_id > 0 ? (m_next_id - 1) : 0;
    }

    std::vector<LiveEvent> get_events_since(uint64_t since_id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<LiveEvent> res;
        for (const auto& ev : m_events) {
            if (ev.id > since_id) {
                res.push_back(ev);
            }
        }
        return res;
    }

    bool wait_for_events(uint64_t since_id, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&]() {
            return m_shutdown.load() || (!m_events.empty() && m_events.back().id > since_id);
        });
    }

    // Async-signal-safe flag setter for signal handlers
    void request_shutdown() {
        m_shutdown.store(true, std::memory_order_relaxed);
    }

    void shutdown() {
        m_shutdown.store(true);
        m_cv.notify_all();
    }

    bool is_shutdown() const {
        return m_shutdown.load();
    }

    void update_presence(int user_id,
                         const std::string& username,
                         const std::string& display_name,
                         const std::string& avatar_color,
                         int item_id,
                         const std::string& item_key,
                         const std::string& action,
                         bool is_active) {
        std::unique_lock<std::mutex> lock(m_presence_mutex);
        prune_presences_locked();

        std::string key = std::to_string(user_id) + "_" + std::to_string(item_id);
        if (!is_active) {
            m_presences.erase(key);
        } else {
            PresenceUser pu;
            pu.user_id = user_id;
            pu.username = username;
            pu.display_name = display_name;
            pu.avatar_color = avatar_color;
            pu.item_id = item_id;
            pu.item_key = item_key;
            pu.action = action.empty() ? "viewing" : action;
            pu.last_seen = std::chrono::steady_clock::now();
            m_presences[key] = pu;
        }
        lock.unlock();

        auto p_json = json::json_value::make_object();
        p_json.add_int("user_id", user_id);
        p_json.add_str("display_name", display_name);
        p_json.add_str("avatar_color", avatar_color);
        p_json.add_int("item_id", item_id);
        p_json.add_str("item_key", item_key);
        p_json.add_str("action", action);
        p_json.add_bool("is_active", is_active);
        publish("presence_updated", p_json.to_string(), user_id, display_name, item_id, item_key);
    }

    std::vector<PresenceUser> get_presences(int item_id = 0) {
        std::lock_guard<std::mutex> lock(m_presence_mutex);
        prune_presences_locked();
        std::vector<PresenceUser> res;
        for (const auto& [k, pu] : m_presences) {
            if (item_id <= 0 || pu.item_id == item_id) {
                res.push_back(pu);
            }
        }
        return res;
    }

private:
    RealtimeEngine() = default;

    void prune_presences_locked() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = m_presences.begin(); it != m_presences.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_seen).count();
            if (elapsed > 35) {
                it = m_presences.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_shutdown{false};
    uint64_t m_next_id = 1;
    std::deque<LiveEvent> m_events;

    std::mutex m_presence_mutex;
    std::map<std::string, PresenceUser> m_presences;
};

inline RealtimeEngine& get_engine() {
    return RealtimeEngine::instance();
}

} // namespace realtime
