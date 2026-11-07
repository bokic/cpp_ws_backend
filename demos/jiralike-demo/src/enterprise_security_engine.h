#pragma once

#include "crypto_utils.h"
#include "qrcodegen.hpp"
#include "raii_json.h"
#include "raii_sqlite.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace enterprise_sec {

// ============================================================================
// 1. QR Code SVG Generator
// ============================================================================

inline std::string generate_qr_svg(const std::string &text, int border = 4) {
    using qrcodegen::QrCode;
    try {
        QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::MEDIUM);
        int size = qr.getSize();
        int total_size = size + border * 2;
        std::ostringstream oss;
        oss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
            << total_size << " " << total_size << "\" stroke=\"none\">\n";
        oss << "\t<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
        oss << "\t<path d=\"";
        bool first = true;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (qr.getModule(x, y)) {
                    if (!first) oss << " ";
                    oss << "M" << (x + border) << "," << (y + border) << "h1v1h-1z";
                    first = false;
                }
            }
        }
        oss << "\" fill=\"#000000\"/>\n";
        oss << "</svg>";
        return oss.str();
    } catch (...) {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"><rect width=\"100\" height=\"100\" fill=\"#EEE\"/></svg>";
    }
}

// ============================================================================
// 2. Enterprise SSO (SAML 2.0 / OIDC) Configuration & Handshake
// ============================================================================

struct sso_config {
    int id = 1;
    bool is_enabled = false;
    std::string protocol = "SAML2"; // 'SAML2' or 'OIDC'
    std::string provider_name = "Enterprise IdP";
    std::string entity_id = "urn:jiralike:sp";
    std::string sso_url = "https://idp.example.com/sso/login";
    std::string issuer = "https://idp.example.com";
    std::string client_id = "jiralike-enterprise-client";
    std::string client_secret = "";
    std::string certificate = "";
    std::string email_domain = "example.com";
    bool auto_provision = true;
    std::string default_role = "Developer";
    std::string updated_at = "";
};

inline sso_config get_sso_config(db::sqlite_db &conn) {
    sso_config cfg;
    auto stmt = conn.prepare(R"SQL(
        SELECT id, is_enabled, protocol, provider_name, entity_id, sso_url,
               issuer, client_id, client_secret, certificate, email_domain,
               auto_provision, default_role, updated_at
        FROM sso_configurations
        WHERE id = 1;
    )SQL");
    if (stmt.step() == SQLITE_ROW) {
        cfg.id = stmt.column_int(0);
        cfg.is_enabled = (stmt.column_int(1) != 0);
        cfg.protocol = stmt.column_str(2);
        cfg.provider_name = stmt.column_str(3);
        cfg.entity_id = stmt.column_str(4);
        cfg.sso_url = stmt.column_str(5);
        cfg.issuer = stmt.column_str(6);
        cfg.client_id = stmt.column_str(7);
        cfg.client_secret = stmt.column_str(8);
        cfg.certificate = stmt.column_str(9);
        cfg.email_domain = stmt.column_str(10);
        cfg.auto_provision = (stmt.column_int(11) != 0);
        cfg.default_role = stmt.column_str(12);
        cfg.updated_at = stmt.column_str(13);
    }
    return cfg;
}

inline json::json_value sso_config_to_json(const sso_config &cfg, bool redact_secret = true) {
    auto obj = json::json_value::make_object();
    obj.add_int("id", cfg.id);
    obj.add_bool("is_enabled", cfg.is_enabled);
    obj.add_str("protocol", cfg.protocol);
    obj.add_str("provider_name", cfg.provider_name);
    obj.add_str("entity_id", cfg.entity_id);
    obj.add_str("sso_url", cfg.sso_url);
    obj.add_str("issuer", cfg.issuer);
    obj.add_str("client_id", cfg.client_id);
    obj.add_str("client_secret", redact_secret && !cfg.client_secret.empty() ? "••••••••" : cfg.client_secret);
    obj.add_str("certificate", cfg.certificate);
    obj.add_str("email_domain", cfg.email_domain);
    obj.add_bool("auto_provision", cfg.auto_provision);
    obj.add_str("default_role", cfg.default_role);
    obj.add_str("updated_at", cfg.updated_at);
    return obj;
}

inline bool save_sso_config(db::sqlite_db &conn, const sso_config &cfg) {
    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare(R"SQL(
        INSERT INTO sso_configurations (
            id, is_enabled, protocol, provider_name, entity_id, sso_url,
            issuer, client_id, client_secret, certificate, email_domain,
            auto_provision, default_role, updated_at
        ) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT(id) DO UPDATE SET
            is_enabled = excluded.is_enabled,
            protocol = excluded.protocol,
            provider_name = excluded.provider_name,
            entity_id = excluded.entity_id,
            sso_url = excluded.sso_url,
            issuer = excluded.issuer,
            client_id = excluded.client_id,
            client_secret = CASE WHEN excluded.client_secret = '••••••••' OR excluded.client_secret = '' THEN sso_configurations.client_secret ELSE excluded.client_secret END,
            certificate = excluded.certificate,
            email_domain = excluded.email_domain,
            auto_provision = excluded.auto_provision,
            default_role = excluded.default_role,
            updated_at = datetime('now');
    )SQL");
    stmt.bind_int(1, cfg.is_enabled ? 1 : 0);
    stmt.bind_text(2, cfg.protocol);
    stmt.bind_text(3, cfg.provider_name);
    stmt.bind_text(4, cfg.entity_id);
    stmt.bind_text(5, cfg.sso_url);
    stmt.bind_text(6, cfg.issuer);
    stmt.bind_text(7, cfg.client_id);
    stmt.bind_text(8, cfg.client_secret);
    stmt.bind_text(9, cfg.certificate);
    stmt.bind_text(10, cfg.email_domain);
    stmt.bind_int(11, cfg.auto_provision ? 1 : 0);
    stmt.bind_text(12, cfg.default_role);
    stmt.step_done();
    tx.commit();
    return true;
}

inline std::string get_sp_metadata_xml(const sso_config &cfg, const std::string &base_url) {
    std::string entity_id = cfg.entity_id.empty() ? "urn:jiralike:sp" : cfg.entity_id;
    std::string acs_url = base_url + "/api/auth/sso/callback";

    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    oss << "<md:EntityDescriptor xmlns:md=\"urn:oasis:names:tc:SAML:2.0:metadata\" entityID=\"" << entity_id << "\">\n";
    oss << "  <md:SPSSODescriptor AuthnRequestsSigned=\"false\" WantAssertionsSigned=\"true\" protocolSupportEnumeration=\"urn:oasis:names:tc:SAML:2.0:protocol\">\n";
    oss << "    <md:NameIDFormat>urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress</md:NameIDFormat>\n";
    oss << "    <md:AssertionConsumerService Binding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\" Location=\"" << acs_url << "\" index=\"1\" isDefault=\"true\"/>\n";
    oss << "  </md:SPSSODescriptor>\n";
    oss << "</md:EntityDescriptor>\n";
    return oss.str();
}

inline json::json_value initiate_sso_login(db::sqlite_db &conn, const std::string &domain, const std::string &base_url) {
    sso_config cfg = get_sso_config(conn);
    auto res = json::json_value::make_object();
    res.add_bool("is_enabled", cfg.is_enabled);
    res.add_str("protocol", cfg.protocol);
    res.add_str("provider_name", cfg.provider_name);

    if (!cfg.is_enabled) {
        res.add_str("error", "Enterprise SSO is currently disabled");
        return res;
    }

    std::string state_nonce = utils::generate_random_hex(16);
    std::string redirect_url;
    if (cfg.protocol == "OIDC") {
        std::string acs_url = base_url + "/api/auth/sso/callback";
        redirect_url = cfg.sso_url + "?response_type=code&client_id=" + cfg.client_id +
                       "&redirect_uri=" + acs_url + "&scope=openid+email+profile&state=" + state_nonce;
    } else {
        // SAML 2.0
        redirect_url = cfg.sso_url + "?SAMLRequest=" + state_nonce + "&RelayState=" + state_nonce;
    }

    res.add_str("redirect_url", redirect_url);
    res.add_str("relay_state", state_nonce);
    return res;
}

struct sso_claims {
    std::string email;
    std::string username;
    std::string display_name;
    std::string role = "Developer";
    bool valid = false;
};

// Parse incoming SAML assertion XML or JSON claims payload
inline sso_claims parse_sso_payload(const std::string &raw_payload) {
    sso_claims claims;
    // Check if JSON
    auto json_val = json::json_value::parse(raw_payload);
    if (json_val) {
        // Direct JSON claims or nested assertion
        std::string email = json_val.get_string("email");
        if (email.empty()) email = json_val.get_string("upn");
        if (email.empty()) email = json_val.get_string("sub");

        std::string name = json_val.get_string("name");
        if (name.empty()) name = json_val.get_string("display_name");
        if (name.empty()) name = json_val.get_string("name_id");

        std::string uname = json_val.get_string("username");
        if (uname.empty() && !email.empty()) {
            auto at = email.find('@');
            uname = (at != std::string::npos) ? email.substr(0, at) : email;
        }

        std::string role = json_val.get_string("role");
        if (role.empty()) role = "Developer";

        if (!email.empty()) {
            claims.email = email;
            claims.username = uname;
            claims.display_name = name.empty() ? uname : name;
            claims.role = role;
            claims.valid = true;
            return claims;
        }
    }

    // Try XML pattern parsing for SAML Assertion:
    // Look for <saml:NameID>...</saml:NameID> or Attribute Name="email"
    std::string xml = raw_payload;
    // If base64-encoded, decode
    if (xml.find('<') == std::string::npos) {
        xml = utils::base64_decode(raw_payload);
    }

    // Extract email from NameID or Attribute
    std::regex name_id_regex("<(?:saml2?:)?NameID[^>]*>([^<]+)</(?:saml2?:)?NameID>", std::regex::icase);
    std::smatch m;
    if (std::regex_search(xml, m, name_id_regex) && m.size() > 1) {
        claims.email = m[1].str();
    }

    // Search for AttributeValue
    std::regex attr_regex("Name=[\"'](?:email|mail|User\\.Email)[\"'][^>]*>[\\s\\S]*?<[^>]*AttributeValue[^>]*>([^<]+)<", std::regex::icase);
    if (std::regex_search(xml, m, attr_regex) && m.size() > 1) {
        claims.email = m[1].str();
    }

    std::regex disp_regex("Name=[\"'](?:displayName|name|User\\.DisplayName)[\"'][^>]*>[\\s\\S]*?<[^>]*AttributeValue[^>]*>([^<]+)<", std::regex::icase);
    if (std::regex_search(xml, m, disp_regex) && m.size() > 1) {
        claims.display_name = m[1].str();
    }

    if (!claims.email.empty()) {
        auto at = claims.email.find('@');
        claims.username = (at != std::string::npos) ? claims.email.substr(0, at) : claims.email;
        if (claims.display_name.empty()) claims.display_name = claims.username;
        claims.valid = true;
    }

    return claims;
}

inline bool process_sso_callback(db::sqlite_db &conn, const sso_claims &claims,
                                int &out_user_id, std::string &out_session_token,
                                std::string &out_error) {
    if (!claims.valid || claims.email.empty()) {
        out_error = "Invalid SSO assertion or missing email claim";
        return false;
    }

    sso_config cfg = get_sso_config(conn);
    if (!cfg.is_enabled) {
        out_error = "Enterprise SSO is currently disabled";
        return false;
    }

    // Domain validation if configured
    if (!cfg.email_domain.empty() && cfg.email_domain != "*") {
        std::string req_suffix = "@" + cfg.email_domain;
        if (claims.email.size() < req_suffix.size() ||
            claims.email.substr(claims.email.size() - req_suffix.size()) != req_suffix) {
            out_error = "Email domain does not match allowed enterprise domain (" + cfg.email_domain + ")";
            return false;
        }
    }

    int user_id = 0;
    std::string existing_username;
    {
        auto stmt = conn.prepare("SELECT id, username FROM users WHERE LOWER(email) = LOWER(?);");
        stmt.bind_text(1, claims.email);
        if (stmt.step() == SQLITE_ROW) {
            user_id = stmt.column_int(0);
            existing_username = stmt.column_str(1);
        }
    }

    if (user_id == 0) {
        // User does not exist
        if (!cfg.auto_provision) {
            out_error = "User not found and Just-In-Time (JIT) provisioning is disabled";
            return false;
        }

        // Auto-provision user
        std::string final_username = claims.username;
        // Ensure username uniqueness
        int suffix = 1;
        while (true) {
            auto chk = conn.prepare("SELECT id FROM users WHERE username = ?;");
            chk.bind_text(1, final_username);
            if (chk.step() != SQLITE_ROW) break;
            final_username = claims.username + std::to_string(suffix++);
        }

        std::string role = cfg.default_role.empty() ? "Developer" : cfg.default_role;
        std::string fake_pwd = utils::hash_password(utils::generate_random_hex(24));
        std::string avatar_color = "#" + utils::generate_random_hex(3);

        db::sqlite_transaction tx(conn);
        auto ins = conn.prepare(R"SQL(
            INSERT INTO users (username, email, password_hash, display_name, role, avatar_color)
            VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        ins.bind_text(1, final_username);
        ins.bind_text(2, claims.email);
        ins.bind_text(3, fake_pwd);
        ins.bind_text(4, claims.display_name.empty() ? final_username : claims.display_name);
        ins.bind_text(5, role);
        ins.bind_text(6, avatar_color);
        ins.step_done();
        user_id = static_cast<int>(conn.last_insert_rowid());
        tx.commit();
    }

    // Issue active session token
    out_session_token = utils::generate_random_hex(32);
    {
        db::sqlite_transaction tx(conn);
        auto sins = conn.prepare(R"SQL(
            INSERT INTO sessions (token, user_id, expires_at)
            VALUES (?, ?, datetime('now', '+7 days'));
        )SQL");
        sins.bind_text(1, out_session_token);
        sins.bind_int(2, user_id);
        sins.step_done();
        tx.commit();
    }

    out_user_id = user_id;
    return true;
}

// ============================================================================
// 3. Two-Factor Authentication (RFC 6238 TOTP)
// ============================================================================

struct totp_setup_result {
    std::string secret;
    std::string otpauth_url;
    std::string qr_svg;
    std::vector<std::string> recovery_codes;
};

inline totp_setup_result initiate_2fa_setup(db::sqlite_db &conn, int user_id, const std::string &username, const std::string &issuer = "JiraLike") {
    totp_setup_result res;
    res.secret = utils::generate_totp_secret(20); // 32 Base32 chars
    res.recovery_codes = utils::generate_recovery_codes(10);

    // Build otpauth URL
    res.otpauth_url = "otpauth://totp/" + issuer + ":" + username +
                      "?secret=" + res.secret + "&issuer=" + issuer + "&algorithm=SHA1&digits=6&period=30";

    // Generate SVG QR Code
    res.qr_svg = generate_qr_svg(res.otpauth_url, 4);

    // Hash recovery codes and serialize to JSON array
    auto arr = json::json_value::make_array();
    for (const auto &code : res.recovery_codes) {
        arr.add_string(utils::sha256_hex(code));
    }
    std::string rec_json = arr.to_string();

    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare(R"SQL(
        INSERT INTO user_2fa (user_id, secret, is_enabled, recovery_codes, created_at, updated_at)
        VALUES (?, ?, 0, ?, datetime('now'), datetime('now'))
        ON CONFLICT(user_id) DO UPDATE SET
            secret = excluded.secret,
            is_enabled = 0,
            recovery_codes = excluded.recovery_codes,
            updated_at = datetime('now');
    )SQL");
    stmt.bind_int(1, user_id);
    stmt.bind_text(2, res.secret);
    stmt.bind_text(3, rec_json);
    stmt.step_done();
    tx.commit();

    return res;
}

inline bool verify_and_enable_2fa(db::sqlite_db &conn, int user_id, const std::string &code) {
    std::string secret;
    {
        auto stmt = conn.prepare("SELECT secret FROM user_2fa WHERE user_id = ?;");
        stmt.bind_int(1, user_id);
        if (stmt.step() == SQLITE_ROW) {
            secret = stmt.column_str(0);
        }
    }
    if (secret.empty()) return false;

    uint64_t now_ts = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    if (!utils::verify_totp(secret, code, now_ts, 1, 30)) {
        return false;
    }

    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare("UPDATE user_2fa SET is_enabled = 1, updated_at = datetime('now') WHERE user_id = ?;");
    stmt.bind_int(1, user_id);
    stmt.step_done();
    tx.commit();
    return true;
}

inline json::json_value get_2fa_status(db::sqlite_db &conn, int user_id) {
    auto res = json::json_value::make_object();
    res.add_bool("is_enabled", false);
    res.add_bool("has_setup", false);
    res.add_int("remaining_recovery_codes", 0);

    auto stmt = conn.prepare("SELECT is_enabled, recovery_codes FROM user_2fa WHERE user_id = ?;");
    stmt.bind_int(1, user_id);
    if (stmt.step() == SQLITE_ROW) {
        int is_en = stmt.column_int(0);
        res.add_bool("is_enabled", is_en != 0);
        res.add_bool("has_setup", true);

        auto codes_json = json::json_value::parse(stmt.column_str(1));
        if (codes_json) {
            res.add_int("remaining_recovery_codes", static_cast<int>(codes_json.array_length()));
        }
    }
    return res;
}

inline bool disable_2fa(db::sqlite_db &conn, int user_id) {
    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare("DELETE FROM user_2fa WHERE user_id = ?;");
    stmt.bind_int(1, user_id);
    stmt.step_done();
    tx.commit();
    return true;
}

inline std::string create_pending_mfa_login(db::sqlite_db &conn, int user_id) {
    std::string token = utils::generate_random_hex(32);
    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare(R"SQL(
        INSERT INTO mfa_pending_logins (token, user_id, expires_at)
        VALUES (?, ?, datetime('now', '+5 minutes'));
    )SQL");
    stmt.bind_text(1, token);
    stmt.bind_int(2, user_id);
    stmt.step_done();
    tx.commit();
    return token;
}

inline bool verify_mfa_login(db::sqlite_db &conn, const std::string &pending_token,
                            const std::string &code, int &out_user_id,
                            std::string &out_session_token, std::string &out_error) {
    int user_id = 0;
    {
        auto stmt = conn.prepare(R"SQL(
            SELECT user_id FROM mfa_pending_logins
            WHERE token = ? AND expires_at > datetime('now');
        )SQL");
        stmt.bind_text(1, pending_token);
        if (stmt.step() == SQLITE_ROW) {
            user_id = stmt.column_int(0);
        }
    }
    if (user_id == 0) {
        out_error = "MFA session token expired or invalid";
        return false;
    }

    std::string secret;
    {
        auto stmt = conn.prepare("SELECT secret FROM user_2fa WHERE user_id = ? AND is_enabled = 1;");
        stmt.bind_int(1, user_id);
        if (stmt.step() == SQLITE_ROW) {
            secret = stmt.column_str(0);
        }
    }
    if (secret.empty()) {
        out_error = "Two-factor authentication is not active for this account";
        return false;
    }

    uint64_t now_ts = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    if (!utils::verify_totp(secret, code, now_ts, 1, 30)) {
        out_error = "Invalid 6-digit TOTP verification code";
        return false;
    }

    // Clean pending token
    {
        db::sqlite_transaction tx(conn);
        auto del = conn.prepare("DELETE FROM mfa_pending_logins WHERE token = ?;");
        del.bind_text(1, pending_token);
        del.step_done();

        // Create new active session
        out_session_token = utils::generate_random_hex(32);
        auto sins = conn.prepare(R"SQL(
            INSERT INTO sessions (token, user_id, expires_at)
            VALUES (?, ?, datetime('now', '+7 days'));
        )SQL");
        sins.bind_text(1, out_session_token);
        sins.bind_int(2, user_id);
        sins.step_done();
        tx.commit();
    }

    out_user_id = user_id;
    return true;
}

inline bool verify_mfa_recovery_login(db::sqlite_db &conn, const std::string &pending_token,
                                     int user_id_param, const std::string &recovery_code,
                                     int &out_user_id, std::string &out_session_token,
                                     int &out_remaining, std::string &out_error) {
    int user_id = user_id_param;
    if (user_id <= 0 && !pending_token.empty()) {
        auto stmt = conn.prepare(R"SQL(
            SELECT user_id FROM mfa_pending_logins
            WHERE token = ? AND expires_at > datetime('now');
        )SQL");
        stmt.bind_text(1, pending_token);
        if (stmt.step() == SQLITE_ROW) {
            user_id = stmt.column_int(0);
        }
    }
    if (user_id <= 0) {
        out_error = "MFA session token expired or invalid";
        return false;
    }

    std::string rec_json_str;
    {
        auto stmt = conn.prepare("SELECT recovery_codes FROM user_2fa WHERE user_id = ? AND is_enabled = 1;");
        stmt.bind_int(1, user_id);
        if (stmt.step() == SQLITE_ROW) {
            rec_json_str = stmt.column_str(0);
        }
    }
    if (rec_json_str.empty()) {
        out_error = "Two-factor authentication is not active for this account";
        return false;
    }

    std::string clean_code = recovery_code;
    // Remove whitespace
    clean_code.erase(std::remove_if(clean_code.begin(), clean_code.end(), ::isspace), clean_code.end());
    std::string hash = utils::sha256_hex(clean_code);

    auto rec_arr = json::json_value::parse(rec_json_str);
    if (!rec_arr) {
        out_error = "No active recovery codes found";
        return false;
    }

    auto new_arr = json::json_value::make_array();
    bool matched = false;
    size_t sz = rec_arr.array_length();
    for (size_t i = 0; i < sz; ++i) {
        auto item = rec_arr.array_get(i);
        const char *s = json_object_get_string(item.get());
        std::string stored_hash = s ? s : "";
        if (!matched && stored_hash == hash) {
            matched = true; // Consume code
        } else {
            new_arr.add_string(stored_hash);
        }
    }

    if (!matched) {
        out_error = "Invalid or already used emergency backup recovery code";
        return false;
    }

    out_remaining = static_cast<int>(new_arr.array_length());

    // Update DB
    {
        db::sqlite_transaction tx(conn);
        auto upd = conn.prepare("UPDATE user_2fa SET recovery_codes = ?, updated_at = datetime('now') WHERE user_id = ?;");
        upd.bind_text(1, new_arr.to_string());
        upd.bind_int(2, user_id);
        upd.step_done();

        if (!pending_token.empty()) {
            auto del = conn.prepare("DELETE FROM mfa_pending_logins WHERE token = ?;");
            del.bind_text(1, pending_token);
            del.step_done();
        }

        out_session_token = utils::generate_random_hex(32);
        auto sins = conn.prepare(R"SQL(
            INSERT INTO sessions (token, user_id, expires_at)
            VALUES (?, ?, datetime('now', '+7 days'));
        )SQL");
        sins.bind_text(1, out_session_token);
        sins.bind_int(2, user_id);
        sins.step_done();
        tx.commit();
    }

    out_user_id = user_id;
    return true;
}

// ============================================================================
// 4. Inbound Email Handlers & Processing Engine
// ============================================================================

struct inbound_mail_handler {
    int id = 0;
    std::string name;
    std::string incoming_address;
    int target_project_id = 1;
    std::string target_project_key;
    int issue_type_id = 0;
    std::string issue_type_name;
    int default_reporter_id = 1;
    std::string default_reporter_name;
    std::string handler_type = "CREATE_OR_COMMENT"; // 'CREATE_OR_COMMENT', 'COMMENT_ONLY', 'CREATE_ONLY'
    std::string spam_keywords = "out of office,auto-reply,unsubscribe,mailer-daemon,delivery status notification";
    bool is_active = true;
    std::string created_at;
    std::string updated_at;
};

inline json::json_value mail_handler_to_json(const inbound_mail_handler &h) {
    auto obj = json::json_value::make_object();
    obj.add_int("id", h.id);
    obj.add_str("name", h.name);
    obj.add_str("incoming_address", h.incoming_address);
    obj.add_int("target_project_id", h.target_project_id);
    obj.add_str("target_project_key", h.target_project_key);
    obj.add_int("issue_type_id", h.issue_type_id);
    obj.add_str("issue_type_name", h.issue_type_name);
    obj.add_int("default_reporter_id", h.default_reporter_id);
    obj.add_str("default_reporter_name", h.default_reporter_name);
    obj.add_str("handler_type", h.handler_type);
    obj.add_str("spam_keywords", h.spam_keywords);
    obj.add_bool("is_active", h.is_active);
    obj.add_str("created_at", h.created_at);
    obj.add_str("updated_at", h.updated_at);
    return obj;
}

inline std::vector<inbound_mail_handler> list_mail_handlers(db::sqlite_db &conn) {
    std::vector<inbound_mail_handler> list;
    auto stmt = conn.prepare(R"SQL(
        SELECT h.id, h.name, h.incoming_address, h.target_project_id, COALESCE(p.key, ''),
               COALESCE(h.issue_type_id, 0), COALESCE(it.name, ''),
               h.default_reporter_id, COALESCE(u.display_name, 'Reporter'),
               h.handler_type, h.spam_keywords, h.is_active, h.created_at, h.updated_at
        FROM inbound_mail_handlers h
        LEFT JOIN projects p ON p.id = h.target_project_id
        LEFT JOIN issue_types it ON it.id = h.issue_type_id
        LEFT JOIN users u ON u.id = h.default_reporter_id
        ORDER BY h.id ASC;
    )SQL");
    while (stmt.step() == SQLITE_ROW) {
        inbound_mail_handler h;
        h.id = stmt.column_int(0);
        h.name = stmt.column_str(1);
        h.incoming_address = stmt.column_str(2);
        h.target_project_id = stmt.column_int(3);
        h.target_project_key = stmt.column_str(4);
        h.issue_type_id = stmt.column_int(5);
        h.issue_type_name = stmt.column_str(6);
        h.default_reporter_id = stmt.column_int(7);
        h.default_reporter_name = stmt.column_str(8);
        h.handler_type = stmt.column_str(9);
        h.spam_keywords = stmt.column_str(10);
        h.is_active = (stmt.column_int(11) != 0);
        h.created_at = stmt.column_str(12);
        h.updated_at = stmt.column_str(13);
        list.push_back(std::move(h));
    }
    return list;
}

inline int create_mail_handler(db::sqlite_db &conn, const inbound_mail_handler &h) {
    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare(R"SQL(
        INSERT INTO inbound_mail_handlers (
            name, incoming_address, target_project_id, issue_type_id,
            default_reporter_id, handler_type, spam_keywords, is_active,
            created_at, updated_at
        ) VALUES (?, ?, ?, CASE WHEN ? > 0 THEN ? ELSE NULL END, ?, ?, ?, ?, datetime('now'), datetime('now'));
    )SQL");
    stmt.bind_text(1, h.name);
    stmt.bind_text(2, h.incoming_address);
    stmt.bind_int(3, h.target_project_id);
    stmt.bind_int(4, h.issue_type_id);
    stmt.bind_int(5, h.issue_type_id);
    stmt.bind_int(6, h.default_reporter_id);
    stmt.bind_text(7, h.handler_type);
    stmt.bind_text(8, h.spam_keywords);
    stmt.bind_int(9, h.is_active ? 1 : 0);
    stmt.step_done();
    int new_id = static_cast<int>(conn.last_insert_rowid());
    tx.commit();
    return new_id;
}

inline bool update_mail_handler(db::sqlite_db &conn, const inbound_mail_handler &h) {
    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare(R"SQL(
        UPDATE inbound_mail_handlers SET
            name = ?,
            incoming_address = ?,
            target_project_id = ?,
            issue_type_id = CASE WHEN ? > 0 THEN ? ELSE NULL END,
            default_reporter_id = ?,
            handler_type = ?,
            spam_keywords = ?,
            is_active = ?,
            updated_at = datetime('now')
        WHERE id = ?;
    )SQL");
    stmt.bind_text(1, h.name);
    stmt.bind_text(2, h.incoming_address);
    stmt.bind_int(3, h.target_project_id);
    stmt.bind_int(4, h.issue_type_id);
    stmt.bind_int(5, h.issue_type_id);
    stmt.bind_int(6, h.default_reporter_id);
    stmt.bind_text(7, h.handler_type);
    stmt.bind_text(8, h.spam_keywords);
    stmt.bind_int(9, h.is_active ? 1 : 0);
    stmt.bind_int(10, h.id);
    stmt.step_done();
    tx.commit();
    return true;
}

inline bool delete_mail_handler(db::sqlite_db &conn, int id) {
    db::sqlite_transaction tx(conn);
    auto stmt = conn.prepare("DELETE FROM inbound_mail_handlers WHERE id = ?;");
    stmt.bind_int(1, id);
    stmt.step_done();
    tx.commit();
    return true;
}

inline json::json_value list_mail_logs(db::sqlite_db &conn, int limit = 50) {
    auto arr = json::json_value::make_array();
    auto stmt = conn.prepare(R"SQL(
        SELECT l.id, l.handler_id, COALESCE(h.name, 'Handler'), l.from_address,
               l.to_address, l.subject, l.status, l.work_item_id, COALESCE(w.key, ''),
               l.comment_id, l.error_message, l.created_at
        FROM inbound_mail_logs l
        LEFT JOIN inbound_mail_handlers h ON h.id = l.handler_id
        LEFT JOIN work_items w ON w.id = l.work_item_id
        ORDER BY l.id DESC
        LIMIT ?;
    )SQL");
    stmt.bind_int(1, limit > 0 ? limit : 50);
    while (stmt.step() == SQLITE_ROW) {
        auto obj = json::json_value::make_object();
        obj.add_int("id", stmt.column_int(0));
        obj.add_int("handler_id", stmt.column_int(1));
        obj.add_str("handler_name", stmt.column_str(2));
        obj.add_str("from_address", stmt.column_str(3));
        obj.add_str("to_address", stmt.column_str(4));
        obj.add_str("subject", stmt.column_str(5));
        obj.add_str("status", stmt.column_str(6));
        obj.add_nullable_int("work_item_id", stmt.column_int(7));
        obj.add_str("work_item_key", stmt.column_str(8));
        obj.add_nullable_int("comment_id", stmt.column_int(9));
        obj.add_str("error_message", stmt.column_str(10));
        obj.add_str("created_at", stmt.column_str(11));
        arr.add(std::move(obj));
    }
    return arr;
}

// Inbound email payload structure
struct email_attachment {
    std::string filename;
    std::string mime_type = "application/octet-stream";
    std::string content_bytes;
};

struct parsed_email {
    std::string from;
    std::string to;
    std::string subject;
    std::string body_plain;
    std::string body_html;
    std::vector<email_attachment> attachments;
};

// MIME / RFC 822 Email Parser
inline parsed_email parse_mime_email(const std::string &raw) {
    parsed_email em;
    std::string header_section;
    std::string body_section;

    auto double_crlf = raw.find("\r\n\r\n");
    if (double_crlf != std::string::npos) {
        header_section = raw.substr(0, double_crlf);
        body_section = raw.substr(double_crlf + 4);
    } else {
        auto double_lf = raw.find("\n\n");
        if (double_lf != std::string::npos) {
            header_section = raw.substr(0, double_lf);
            body_section = raw.substr(double_lf + 2);
        } else {
            body_section = raw;
        }
    }

    std::istringstream h_stream(header_section);
    std::string line;
    std::string content_type;

    while (std::getline(h_stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(val.begin());

            std::string lk = key;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);

            if (lk == "from") em.from = val;
            else if (lk == "to") em.to = val;
            else if (lk == "subject") em.subject = val;
            else if (lk == "content-type") content_type = val;
        }
    }

    // Clean email addresses (e.g. "John Doe <john@example.com>" -> "john@example.com")
    auto clean_email = [](const std::string &raw_addr) -> std::string {
        auto lt = raw_addr.find('<');
        auto gt = raw_addr.find('>', lt);
        if (lt != std::string::npos && gt != std::string::npos) {
            return raw_addr.substr(lt + 1, gt - lt - 1);
        }
        std::string s = raw_addr;
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        return s;
    };

    em.from = clean_email(em.from);
    em.to = clean_email(em.to);

    // If multipart
    if (content_type.find("multipart/") != std::string::npos) {
        auto bpos = content_type.find("boundary=");
        if (bpos != std::string::npos) {
            std::string bnd = content_type.substr(bpos + 9);
            if (!bnd.empty() && bnd.front() == '"') {
                bnd.erase(0, 1);
                auto q = bnd.find('"');
                if (q != std::string::npos) bnd = bnd.substr(0, q);
            }
            std::string sep = "--" + bnd;
            size_t p = body_section.find(sep);
            while (p != std::string::npos) {
                p += sep.size();
                if (p + 2 <= body_section.size() && body_section.compare(p, 2, "--") == 0) break;
                if (p + 2 <= body_section.size() && body_section.compare(p, 2, "\r\n") == 0) p += 2;
                else if (p + 1 <= body_section.size() && body_section[p] == '\n') p += 1;

                auto header_end = body_section.find("\r\n\r\n", p);
                size_t skip = 4;
                if (header_end == std::string::npos) {
                    header_end = body_section.find("\n\n", p);
                    skip = 2;
                }
                if (header_end == std::string::npos) break;

                std::string part_hdr = body_section.substr(p, header_end - p);
                size_t data_start = header_end + skip;
                size_t next_sep = body_section.find(sep, data_start);
                if (next_sep == std::string::npos) break;

                size_t data_len = next_sep - data_start;
                if (data_len >= 2 && body_section.compare(data_start + data_len - 2, 2, "\r\n") == 0) data_len -= 2;
                else if (data_len >= 1 && body_section[data_start + data_len - 1] == '\n') data_len -= 1;

                std::string part_data = body_section.substr(data_start, data_len);

                // Check part headers
                if (part_hdr.find("filename=") != std::string::npos || part_hdr.find("attachment") != std::string::npos) {
                    email_attachment att;
                    auto fpos = part_hdr.find("filename=\"");
                    if (fpos != std::string::npos) {
                        fpos += 10;
                        auto fend = part_hdr.find('"', fpos);
                        if (fend != std::string::npos) att.filename = part_hdr.substr(fpos, fend - fpos);
                    }
                    if (att.filename.empty()) att.filename = "attachment.bin";

                    if (part_hdr.find("base64") != std::string::npos) {
                        att.content_bytes = utils::base64_decode(part_data);
                    } else {
                        att.content_bytes = part_data;
                    }
                    em.attachments.push_back(std::move(att));
                } else if (part_hdr.find("text/plain") != std::string::npos) {
                    if (part_hdr.find("base64") != std::string::npos) em.body_plain = utils::base64_decode(part_data);
                    else em.body_plain = part_data;
                } else if (part_hdr.find("text/html") != std::string::npos) {
                    if (part_hdr.find("base64") != std::string::npos) em.body_html = utils::base64_decode(part_data);
                    else em.body_html = part_data;
                }

                p = next_sep;
            }
        }
    } else {
        em.body_plain = body_section;
    }

    if (em.body_plain.empty() && !em.body_html.empty()) {
        // Strip HTML tags for plain body
        std::regex tag_re("<[^>]+>");
        em.body_plain = std::regex_replace(em.body_html, tag_re, "");
    }

    return em;
}

// Inbound Email Processing Engine
inline bool process_inbound_email(db::sqlite_db &conn, const std::string &blobs_dir,
                                 const parsed_email &em, json::json_value &out_res,
                                 std::string &out_error) {
    if (em.to.empty() || em.from.empty()) {
        out_error = "Missing required email 'from' or 'to' address";
        return false;
    }

    // 1. Find matching handler
    inbound_mail_handler handler;
    bool found = false;
    {
        auto stmt = conn.prepare(R"SQL(
            SELECT id, name, incoming_address, target_project_id, issue_type_id,
                   default_reporter_id, handler_type, spam_keywords, is_active
            FROM inbound_mail_handlers
            WHERE (LOWER(incoming_address) = LOWER(?) OR incoming_address = '*') AND is_active = 1
            ORDER BY CASE WHEN incoming_address = '*' THEN 2 ELSE 1 END, id ASC
            LIMIT 1;
        )SQL");
        stmt.bind_text(1, em.to);
        if (stmt.step() == SQLITE_ROW) {
            handler.id = stmt.column_int(0);
            handler.name = stmt.column_str(1);
            handler.incoming_address = stmt.column_str(2);
            handler.target_project_id = stmt.column_int(3);
            handler.issue_type_id = stmt.column_int(4);
            handler.default_reporter_id = stmt.column_int(5);
            handler.handler_type = stmt.column_str(6);
            handler.spam_keywords = stmt.column_str(7);
            handler.is_active = (stmt.column_int(8) != 0);
            found = true;
        }
    }

    if (!found) {
        // Log rejection
        db::sqlite_transaction tx(conn);
        auto l_ins = conn.prepare(R"SQL(
            INSERT INTO inbound_mail_logs (from_address, to_address, subject, status, error_message)
            VALUES (?, ?, ?, 'REJECTED_UNKNOWN', 'No active inbound mail handler configured for recipient');
        )SQL");
        l_ins.bind_text(1, em.from);
        l_ins.bind_text(2, em.to);
        l_ins.bind_text(3, em.subject);
        l_ins.step_done();
        tx.commit();

        out_error = "No active inbound mail handler configured for " + em.to;
        return false;
    }

    // 2. Spam filtering
    std::string sub_lower = em.subject;
    std::transform(sub_lower.begin(), sub_lower.end(), sub_lower.begin(), ::tolower);
    std::string body_lower = em.body_plain;
    std::transform(body_lower.begin(), body_lower.end(), body_lower.begin(), ::tolower);

    // Parse comma-separated spam keywords
    std::stringstream ss(handler.spam_keywords);
    std::string kw;
    bool is_spam = false;
    std::string matched_kw;
    while (std::getline(ss, kw, ',')) {
        while (!kw.empty() && kw.front() == ' ') kw.erase(kw.begin());
        while (!kw.empty() && kw.back() == ' ') kw.pop_back();
        if (kw.empty()) continue;

        std::string kw_lower = kw;
        std::transform(kw_lower.begin(), kw_lower.end(), kw_lower.begin(), ::tolower);

        if (sub_lower.find(kw_lower) != std::string::npos || body_lower.find(kw_lower) != std::string::npos) {
            is_spam = true;
            matched_kw = kw;
            break;
        }
    }

    if (is_spam) {
        db::sqlite_transaction tx(conn);
        auto l_ins = conn.prepare(R"SQL(
            INSERT INTO inbound_mail_logs (handler_id, from_address, to_address, subject, status, error_message)
            VALUES (?, ?, ?, ?, 'REJECTED_SPAM', ?);
        )SQL");
        l_ins.bind_int(1, handler.id);
        l_ins.bind_text(2, em.from);
        l_ins.bind_text(3, em.to);
        l_ins.bind_text(4, em.subject);
        l_ins.bind_text(5, "Blocked by spam filter keyword: '" + matched_kw + "'");
        l_ins.step_done();
        tx.commit();

        out_error = "Email flagged and blocked as automated reply/spam keyword ('" + matched_kw + "')";
        return false;
    }

    // 3. Sender user matching
    int reporter_id = 0;
    std::string reporter_name = "Email User";
    {
        auto u_stmt = conn.prepare("SELECT id, display_name FROM users WHERE LOWER(email) = LOWER(?);");
        u_stmt.bind_text(1, em.from);
        if (u_stmt.step() == SQLITE_ROW) {
            reporter_id = u_stmt.column_int(0);
            reporter_name = u_stmt.column_str(1);
        }
    }
    if (reporter_id == 0) {
        reporter_id = handler.default_reporter_id;
        // Lookup default reporter name
        auto d_stmt = conn.prepare("SELECT display_name FROM users WHERE id = ?;");
        d_stmt.bind_int(1, reporter_id);
        if (d_stmt.step() == SQLITE_ROW) {
            reporter_name = d_stmt.column_str(0);
        }
    }

    // 4. Threading / Existing Issue matching: Search subject for issue key pattern [A-Z0-9]+-\d+
    std::regex key_regex("([A-Z][A-Z0-9]+-\\d+)", std::regex::icase);
    std::smatch m;
    int existing_item_id = 0;
    std::string matched_key;
    if (std::regex_search(em.subject, m, key_regex) && m.size() > 1) {
        matched_key = m[1].str();
        std::transform(matched_key.begin(), matched_key.end(), matched_key.begin(), ::toupper);
        auto w_stmt = conn.prepare("SELECT id FROM work_items WHERE key = ?;");
        w_stmt.bind_text(1, matched_key);
        if (w_stmt.step() == SQLITE_ROW) {
            existing_item_id = w_stmt.column_int(0);
        }
    }

    if (existing_item_id > 0) {
        // APPEND COMMENT TO EXISTING WORK ITEM
        int comment_id = 0;
        {
            db::sqlite_transaction tx(conn);
            auto c_ins = conn.prepare(R"SQL(
                INSERT INTO comments (work_item_id, author_id, body)
                VALUES (?, ?, ?);
            )SQL");
            c_ins.bind_int(1, existing_item_id);
            c_ins.bind_int(2, reporter_id);
            std::string c_body = em.body_plain.empty() ? "(Empty email message)" : em.body_plain;
            c_ins.bind_text(3, c_body);
            c_ins.step_done();
            comment_id = static_cast<int>(conn.last_insert_rowid());

            // Process attachments
            for (const auto &att : em.attachments) {
                if (att.content_bytes.empty()) continue;
                std::string orig_name = utils::sanitize_filename(att.filename);
                std::string blob_name = "att_" + utils::generate_random_hex(16) + "_" + orig_name;
                std::filesystem::create_directories(blobs_dir);
                std::filesystem::path full_p = std::filesystem::path(blobs_dir) / blob_name;
                std::ofstream ofs(full_p, std::ios::binary);
                ofs.write(att.content_bytes.data(), static_cast<std::streamsize>(att.content_bytes.size()));
                ofs.close();

                auto a_ins = conn.prepare(R"SQL(
                    INSERT INTO attachments (work_item_id, uploader_id, original_filename, mime_type, size_bytes, blob_filename)
                    VALUES (?, ?, ?, ?, ?, ?);
                )SQL");
                a_ins.bind_int(1, existing_item_id);
                a_ins.bind_int(2, reporter_id);
                a_ins.bind_text(3, orig_name);
                a_ins.bind_text(4, att.mime_type);
                a_ins.bind_int(5, static_cast<int>(att.content_bytes.size()));
                a_ins.bind_text(6, blob_name);
                a_ins.step_done();
            }

            // Update work_item updated_at
            auto w_upd = conn.prepare("UPDATE work_items SET updated_at = datetime('now') WHERE id = ?;");
            w_upd.bind_int(1, existing_item_id);
            w_upd.step_done();

            // Insert log
            auto l_ins = conn.prepare(R"SQL(
                INSERT INTO inbound_mail_logs (handler_id, from_address, to_address, subject, status, work_item_id, comment_id)
                VALUES (?, ?, ?, ?, 'PROCESSED', ?, ?);
            )SQL");
            l_ins.bind_int(1, handler.id);
            l_ins.bind_text(2, em.from);
            l_ins.bind_text(3, em.to);
            l_ins.bind_text(4, em.subject);
            l_ins.bind_int(5, existing_item_id);
            l_ins.bind_int(6, comment_id);
            l_ins.step_done();

            tx.commit();
        }

        out_res = json::json_value::make_object();
        out_res.add_bool("ok", true);
        out_res.add_str("action", "comment_added");
        out_res.add_int("work_item_id", existing_item_id);
        out_res.add_str("work_item_key", matched_key);
        out_res.add_int("comment_id", comment_id);
        out_res.add_int("attachments_count", static_cast<int>(em.attachments.size()));
        return true;
    }

    // 5. No existing issue: Check if creation is allowed
    if (handler.handler_type == "COMMENT_ONLY") {
        db::sqlite_transaction tx(conn);
        auto l_ins = conn.prepare(R"SQL(
            INSERT INTO inbound_mail_logs (handler_id, from_address, to_address, subject, status, error_message)
            VALUES (?, ?, ?, ?, 'REJECTED_NO_ISSUE', 'Handler configured for comment only, but subject did not reference valid issue key');
        )SQL");
        l_ins.bind_int(1, handler.id);
        l_ins.bind_text(2, em.from);
        l_ins.bind_text(3, em.to);
        l_ins.bind_text(4, em.subject);
        l_ins.step_done();
        tx.commit();

        out_error = "Handler is COMMENT_ONLY, but subject did not contain existing issue key";
        return false;
    }

    // 6. CREATE NEW WORK ITEM
    std::string proj_key;
    int next_seq = 1;
    {
        auto seq_stmt = conn.prepare(R"SQL(
            UPDATE projects SET issue_seq = issue_seq + 1
            WHERE id = ?
            RETURNING key, issue_seq;
        )SQL");
        seq_stmt.bind_int(1, handler.target_project_id);
        if (seq_stmt.step() == SQLITE_ROW) {
            proj_key = seq_stmt.column_str(0);
            next_seq = seq_stmt.column_int(1);
        }
    }
    if (proj_key.empty()) {
        out_error = "Target project for mail handler not found";
        return false;
    }

    std::string new_key = proj_key + "-" + std::to_string(next_seq);
    std::string issue_type_name = "Task";
    if (handler.issue_type_id > 0) {
        auto t_stmt = conn.prepare("SELECT name FROM issue_types WHERE id = ?;");
        t_stmt.bind_int(1, handler.issue_type_id);
        if (t_stmt.step() == SQLITE_ROW) {
            issue_type_name = t_stmt.column_str(0);
        }
    }

    std::string title = em.subject;
    // Clean leading Re:, Fwd:, etc.
    std::regex pfx_re("^(?:(?:re|fwd|fw|external):\\s*)+", std::regex::icase);
    title = std::regex_replace(title, pfx_re, "");
    if (title.empty()) title = "Email from " + em.from;

    int new_work_item_id = 0;
    {
        db::sqlite_transaction tx(conn);
        auto w_ins = conn.prepare(R"SQL(
            INSERT INTO work_items (
                project_id, key, type, category, status, priority, title, description,
                reporter_id, board_order, created_at, updated_at
            ) VALUES (?, ?, ?, 'Core', 'To Do', 'Medium', ?, ?, ?, 0, datetime('now'), datetime('now'));
        )SQL");
        w_ins.bind_int(1, handler.target_project_id);
        w_ins.bind_text(2, new_key);
        w_ins.bind_text(3, issue_type_name);
        w_ins.bind_text(4, title);
        w_ins.bind_text(5, em.body_plain.empty() ? "(No description provided)" : em.body_plain);
        w_ins.bind_int(6, reporter_id);
        w_ins.step_done();
        new_work_item_id = static_cast<int>(conn.last_insert_rowid());

        // Process attachments
        for (const auto &att : em.attachments) {
            if (att.content_bytes.empty()) continue;
            std::string orig_name = utils::sanitize_filename(att.filename);
            std::string blob_name = "att_" + utils::generate_random_hex(16) + "_" + orig_name;
            std::filesystem::create_directories(blobs_dir);
            std::filesystem::path full_p = std::filesystem::path(blobs_dir) / blob_name;
            std::ofstream ofs(full_p, std::ios::binary);
            ofs.write(att.content_bytes.data(), static_cast<std::streamsize>(att.content_bytes.size()));
            ofs.close();

            auto a_ins = conn.prepare(R"SQL(
                INSERT INTO attachments (work_item_id, uploader_id, original_filename, mime_type, size_bytes, blob_filename)
                VALUES (?, ?, ?, ?, ?, ?);
            )SQL");
            a_ins.bind_int(1, new_work_item_id);
            a_ins.bind_int(2, reporter_id);
            a_ins.bind_text(3, orig_name);
            a_ins.bind_text(4, att.mime_type);
            a_ins.bind_int(5, static_cast<int>(att.content_bytes.size()));
            a_ins.bind_text(6, blob_name);
            a_ins.step_done();
        }

        // Add initial history entry
        auto h_ins = conn.prepare(R"SQL(
            INSERT INTO workflow_history (work_item_id, from_status, to_status, changed_by, comment)
            VALUES (?, '', 'To Do', ?, 'Created via inbound email processor');
        )SQL");
        h_ins.bind_int(1, new_work_item_id);
        h_ins.bind_int(2, reporter_id);
        h_ins.step_done();

        // Insert log
        auto l_ins = conn.prepare(R"SQL(
            INSERT INTO inbound_mail_logs (handler_id, from_address, to_address, subject, status, work_item_id)
            VALUES (?, ?, ?, ?, 'PROCESSED', ?);
        )SQL");
        l_ins.bind_int(1, handler.id);
        l_ins.bind_text(2, em.from);
        l_ins.bind_text(3, em.to);
        l_ins.bind_text(4, em.subject);
        l_ins.bind_int(5, new_work_item_id);
        l_ins.step_done();

        tx.commit();
    }

    out_res = json::json_value::make_object();
    out_res.add_bool("ok", true);
    out_res.add_str("action", "issue_created");
    out_res.add_int("work_item_id", new_work_item_id);
    out_res.add_str("work_item_key", new_key);
    out_res.add_str("title", title);
    out_res.add_int("attachments_count", static_cast<int>(em.attachments.size()));
    return true;
}

} // namespace enterprise_sec