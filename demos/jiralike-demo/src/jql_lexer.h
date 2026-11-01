#pragma once

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace jql {

enum class TokenType {
    Identifier,
    CustomFieldRef, // cf[1001] or cf[environment]
    String,
    Number,
    Operator,       // =, !=, ~, !~, >, >=, <, <=
    And,            // AND
    Or,             // OR
    Not,            // NOT
    In,             // IN
    NotIn,          // NOT IN
    Is,             // IS
    IsNot,          // IS NOT
    Was,            // WAS
    WasIn,          // WAS IN
    WasNot,         // WAS NOT
    WasNotIn,       // WAS NOT IN
    Changed,        // CHANGED
    OrderBy,        // ORDER BY
    Asc,            // ASC
    Desc,           // DESC
    Empty,          // EMPTY, NULL
    From,           // FROM
    To,             // TO
    By,             // BY
    During,         // DURING
    Before,         // BEFORE
    After,          // AFTER
    On,             // ON
    LParen,         // (
    RParen,         // )
    Comma,          // ,
    EndOfInput
};

inline const char *token_type_name(TokenType t) {
    switch (t) {
    case TokenType::Identifier:     return "IDENTIFIER";
    case TokenType::CustomFieldRef: return "CUSTOM_FIELD";
    case TokenType::String:         return "STRING";
    case TokenType::Number:         return "NUMBER";
    case TokenType::Operator:       return "OPERATOR";
    case TokenType::And:            return "AND";
    case TokenType::Or:             return "OR";
    case TokenType::Not:            return "NOT";
    case TokenType::In:             return "IN";
    case TokenType::NotIn:          return "NOT_IN";
    case TokenType::Is:             return "IS";
    case TokenType::IsNot:          return "IS_NOT";
    case TokenType::Was:            return "WAS";
    case TokenType::WasIn:          return "WAS_IN";
    case TokenType::WasNot:         return "WAS_NOT";
    case TokenType::WasNotIn:       return "WAS_NOT_IN";
    case TokenType::Changed:        return "CHANGED";
    case TokenType::OrderBy:        return "ORDER_BY";
    case TokenType::Asc:            return "ASC";
    case TokenType::Desc:           return "DESC";
    case TokenType::Empty:          return "EMPTY";
    case TokenType::From:           return "FROM";
    case TokenType::To:             return "TO";
    case TokenType::By:             return "BY";
    case TokenType::During:         return "DURING";
    case TokenType::Before:         return "BEFORE";
    case TokenType::After:          return "AFTER";
    case TokenType::On:             return "ON";
    case TokenType::LParen:         return "LPAREN";
    case TokenType::RParen:         return "RPAREN";
    case TokenType::Comma:          return "COMMA";
    case TokenType::EndOfInput:     return "EOF";
    }
    return "UNKNOWN";
}

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string text;
    std::string raw_text;
    size_t pos = 0;
    size_t length = 0;
    int line = 1;
    int col = 1;
};

class JqlSyntaxError : public std::runtime_error {
public:
    size_t pos;
    size_t length;
    int line;
    int col;

    JqlSyntaxError(const std::string &msg, size_t p, size_t len = 1, int l = 1, int c = 1)
        : std::runtime_error(msg), pos(p), length(len), line(l), col(c) {}
};

inline std::string to_upper_ascii(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

inline std::string to_lower_ascii(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

class JqlLexer {
public:
    explicit JqlLexer(std::string_view input) : input_(input) {}

    std::vector<Token> tokenize() {
        std::vector<Token> raw_tokens;
        pos_ = 0;
        line_ = 1;
        col_ = 1;

        while (pos_ < input_.size()) {
            skip_whitespace();
            if (pos_ >= input_.size()) break;

            size_t start_pos = pos_;
            int start_line = line_;
            int start_col = col_;
            char ch = input_[pos_];

            // Parentheses & Comma
            if (ch == '(') {
                advance();
                raw_tokens.push_back({TokenType::LParen, "(", "(", start_pos, 1, start_line, start_col});
                continue;
            }
            if (ch == ')') {
                advance();
                raw_tokens.push_back({TokenType::RParen, ")", ")", start_pos, 1, start_line, start_col});
                continue;
            }
            if (ch == ',') {
                advance();
                raw_tokens.push_back({TokenType::Comma, ",", ",", start_pos, 1, start_line, start_col});
                continue;
            }

            // Quoted Strings ("..." or '...')
            if (ch == '"' || ch == '\'') {
                raw_tokens.push_back(read_string());
                continue;
            }

            // Operators: =, !=, ~, !~, >, >=, <, <=
            if (ch == '=') {
                advance();
                raw_tokens.push_back({TokenType::Operator, "=", "=", start_pos, 1, start_line, start_col});
                continue;
            }
            if (ch == '~') {
                advance();
                raw_tokens.push_back({TokenType::Operator, "~", "~", start_pos, 1, start_line, start_col});
                continue;
            }
            if (ch == '!') {
                advance();
                if (pos_ < input_.size() && input_[pos_] == '=') {
                    advance();
                    raw_tokens.push_back({TokenType::Operator, "!=", "!=", start_pos, 2, start_line, start_col});
                    continue;
                }
                if (pos_ < input_.size() && input_[pos_] == '~') {
                    advance();
                    raw_tokens.push_back({TokenType::Operator, "!~", "!~", start_pos, 2, start_line, start_col});
                    continue;
                }
                // Standalone '!' acts as NOT
                raw_tokens.push_back({TokenType::Not, "NOT", "!", start_pos, 1, start_line, start_col});
                continue;
            }
            if (ch == '>') {
                advance();
                if (pos_ < input_.size() && input_[pos_] == '=') {
                    advance();
                    raw_tokens.push_back({TokenType::Operator, ">=", ">=", start_pos, 2, start_line, start_col});
                } else {
                    raw_tokens.push_back({TokenType::Operator, ">", ">", start_pos, 1, start_line, start_col});
                }
                continue;
            }
            if (ch == '<') {
                advance();
                if (pos_ < input_.size() && input_[pos_] == '=') {
                    advance();
                    raw_tokens.push_back({TokenType::Operator, "<=", "<=", start_pos, 2, start_line, start_col});
                } else {
                    raw_tokens.push_back({TokenType::Operator, "<", "<", start_pos, 1, start_line, start_col});
                }
                continue;
            }

            // Symbolic logical operators && and ||
            if (ch == '&' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '&') {
                advance();
                advance();
                raw_tokens.push_back({TokenType::And, "AND", "&&", start_pos, 2, start_line, start_col});
                continue;
            }
            if (ch == '|' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '|') {
                advance();
                advance();
                raw_tokens.push_back({TokenType::Or, "OR", "||", start_pos, 2, start_line, start_col});
                continue;
            }

            // Custom field syntax cf[1001] or cf[environment]
            if ((ch == 'c' || ch == 'C') &&
                pos_ + 2 < input_.size() &&
                (input_[pos_ + 1] == 'f' || input_[pos_ + 1] == 'F') &&
                input_[pos_ + 2] == '[') {
                raw_tokens.push_back(read_custom_field_ref());
                continue;
            }

            // Numbers (or date/offset literals like 2026-11-01, -7d, +2w)
            if (std::isdigit(static_cast<unsigned char>(ch)) ||
                ((ch == '-' || ch == '+') && pos_ + 1 < input_.size() &&
                 std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])))) {
                raw_tokens.push_back(read_number_or_literal());
                continue;
            }

            // Identifiers & Keywords (including issue keys like JIRA-101)
            if (is_ident_start(ch)) {
                raw_tokens.push_back(read_identifier_or_keyword());
                continue;
            }

            throw JqlSyntaxError(
                std::string("Unexpected character '") + ch + "' at line " +
                    std::to_string(start_line) + ", col " + std::to_string(start_col),
                start_pos, 1, start_line, start_col);
        }

        // Collapse compound JQL keywords:
        // NOT IN, IS NOT, WAS NOT IN, WAS IN, WAS NOT, ORDER BY
        std::vector<Token> merged;
        merged.reserve(raw_tokens.size() + 1);

        for (size_t i = 0; i < raw_tokens.size(); ++i) {
            const Token &cur = raw_tokens[i];

            // NOT IN
            if (cur.type == TokenType::Not && i + 1 < raw_tokens.size() &&
                raw_tokens[i + 1].type == TokenType::In) {
                const Token &nxt = raw_tokens[i + 1];
                size_t len = (nxt.pos + nxt.length) - cur.pos;
                merged.push_back({
                    TokenType::NotIn,
                    "NOT IN",
                    std::string(input_.substr(cur.pos, len)),
                    cur.pos,
                    len,
                    cur.line,
                    cur.col
                });
                ++i;
                continue;
            }

            // IS NOT
            if (cur.type == TokenType::Is && i + 1 < raw_tokens.size() &&
                raw_tokens[i + 1].type == TokenType::Not) {
                const Token &nxt = raw_tokens[i + 1];
                size_t len = (nxt.pos + nxt.length) - cur.pos;
                merged.push_back({
                    TokenType::IsNot,
                    "IS NOT",
                    std::string(input_.substr(cur.pos, len)),
                    cur.pos,
                    len,
                    cur.line,
                    cur.col
                });
                ++i;
                continue;
            }

            // WAS NOT IN, WAS NOT, WAS IN
            if (cur.type == TokenType::Was) {
                if (i + 2 < raw_tokens.size() &&
                    raw_tokens[i + 1].type == TokenType::Not &&
                    raw_tokens[i + 2].type == TokenType::In) {
                    const Token &end_tok = raw_tokens[i + 2];
                    size_t len = (end_tok.pos + end_tok.length) - cur.pos;
                    merged.push_back({
                        TokenType::WasNotIn,
                        "WAS NOT IN",
                        std::string(input_.substr(cur.pos, len)),
                        cur.pos,
                        len,
                        cur.line,
                        cur.col
                    });
                    i += 2;
                    continue;
                }
                if (i + 1 < raw_tokens.size() && raw_tokens[i + 1].type == TokenType::In) {
                    const Token &nxt = raw_tokens[i + 1];
                    size_t len = (nxt.pos + nxt.length) - cur.pos;
                    merged.push_back({
                        TokenType::WasIn,
                        "WAS IN",
                        std::string(input_.substr(cur.pos, len)),
                        cur.pos,
                        len,
                        cur.line,
                        cur.col
                    });
                    ++i;
                    continue;
                }
                if (i + 1 < raw_tokens.size() && raw_tokens[i + 1].type == TokenType::Not) {
                    const Token &nxt = raw_tokens[i + 1];
                    size_t len = (nxt.pos + nxt.length) - cur.pos;
                    merged.push_back({
                        TokenType::WasNot,
                        "WAS NOT",
                        std::string(input_.substr(cur.pos, len)),
                        cur.pos,
                        len,
                        cur.line,
                        cur.col
                    });
                    ++i;
                    continue;
                }
            }

            // ORDER BY
            if (cur.type == TokenType::Identifier &&
                to_upper_ascii(cur.text) == "ORDER" &&
                i + 1 < raw_tokens.size() &&
                raw_tokens[i + 1].type == TokenType::By) {
                const Token &nxt = raw_tokens[i + 1];
                size_t len = (nxt.pos + nxt.length) - cur.pos;
                merged.push_back({
                    TokenType::OrderBy,
                    "ORDER BY",
                    std::string(input_.substr(cur.pos, len)),
                    cur.pos,
                    len,
                    cur.line,
                    cur.col
                });
                ++i;
                continue;
            }

            merged.push_back(cur);
        }

        merged.push_back({TokenType::EndOfInput, "", "", input_.size(), 0, line_, col_});
        return merged;
    }

private:
    std::string_view input_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    void advance() {
        if (pos_ < input_.size()) {
            if (input_[pos_] == '\n') {
                ++line_;
                col_ = 1;
            } else {
                ++col_;
            }
            ++pos_;
        }
    }

    void skip_whitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            advance();
        }
    }

    static bool is_ident_start(char c) {
        auto uc = static_cast<unsigned char>(c);
        return std::isalpha(uc) || c == '_' || c == '@';
    }

    static bool is_ident_part(char c) {
        auto uc = static_cast<unsigned char>(c);
        return std::isalnum(uc) || c == '_' || c == '-' || c == '.' || c == '@' || c == '/';
    }

    Token read_string() {
        size_t start_pos = pos_;
        int start_line = line_;
        int start_col = col_;
        char quote = input_[pos_];
        advance(); // consume opening quote

        std::string val;
        bool closed = false;
        while (pos_ < input_.size()) {
            char ch = input_[pos_];
            if (ch == '\\' && pos_ + 1 < input_.size()) {
                advance();
                char esc = input_[pos_];
                switch (esc) {
                case 'n': val.push_back('\n'); break;
                case 't': val.push_back('\t'); break;
                case 'r': val.push_back('\r'); break;
                case '"': val.push_back('"'); break;
                case '\'': val.push_back('\''); break;
                case '\\': val.push_back('\\'); break;
                default: val.push_back(esc); break;
                }
                advance();
                continue;
            }
            if (ch == quote) {
                advance();
                closed = true;
                break;
            }
            val.push_back(ch);
            advance();
        }

        if (!closed) {
            throw JqlSyntaxError(
                "Unterminated string literal starting at line " +
                    std::to_string(start_line) + ", col " + std::to_string(start_col),
                start_pos, pos_ - start_pos, start_line, start_col);
        }

        size_t len = pos_ - start_pos;
        return {
            TokenType::String,
            val,
            std::string(input_.substr(start_pos, len)),
            start_pos,
            len,
            start_line,
            start_col
        };
    }

    Token read_custom_field_ref() {
        size_t start_pos = pos_;
        int start_line = line_;
        int start_col = col_;
        advance(); // 'c'
        advance(); // 'f'
        advance(); // '['

        std::string inner;
        bool closed = false;
        while (pos_ < input_.size()) {
            char ch = input_[pos_];
            if (ch == ']') {
                advance();
                closed = true;
                break;
            }
            inner.push_back(ch);
            advance();
        }
        if (!closed || inner.empty()) {
            throw JqlSyntaxError(
                "Invalid custom field reference (expected cf[id_or_key]) at line " +
                    std::to_string(start_line) + ", col " + std::to_string(start_col),
                start_pos, pos_ - start_pos, start_line, start_col);
        }

        // Trim quotes or whitespace inside cf[...]
        while (!inner.empty() && (inner.front() == ' ' || inner.front() == '"' || inner.front() == '\'')) {
            inner.erase(inner.begin());
        }
        while (!inner.empty() && (inner.back() == ' ' || inner.back() == '"' || inner.back() == '\'')) {
            inner.pop_back();
        }

        size_t len = pos_ - start_pos;
        return {
            TokenType::CustomFieldRef,
            "cf[" + inner + "]",
            std::string(input_.substr(start_pos, len)),
            start_pos,
            len,
            start_line,
            start_col
        };
    }

    Token read_number_or_literal() {
        size_t start_pos = pos_;
        int start_line = line_;
        int start_col = col_;

        if (input_[pos_] == '-' || input_[pos_] == '+') {
            advance();
        }
        bool is_pure_number = true;
        bool seen_dot = false;

        while (pos_ < input_.size()) {
            char ch = input_[pos_];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                advance();
            } else if (ch == '.' && !seen_dot) {
                seen_dot = true;
                advance();
            } else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '-' || ch == ':') {
                // e.g., date literal 2026-11-01 or offset -7d, +2w
                is_pure_number = false;
                advance();
            } else {
                break;
            }
        }

        size_t len = pos_ - start_pos;
        std::string txt(input_.substr(start_pos, len));
        return {
            is_pure_number ? TokenType::Number : TokenType::String,
            txt,
            txt,
            start_pos,
            len,
            start_line,
            start_col
        };
    }

    Token read_identifier_or_keyword() {
        size_t start_pos = pos_;
        int start_line = line_;
        int start_col = col_;

        while (pos_ < input_.size() && is_ident_part(input_[pos_])) {
            advance();
        }

        size_t len = pos_ - start_pos;
        std::string txt(input_.substr(start_pos, len));
        std::string upper = to_upper_ascii(txt);

        TokenType type = TokenType::Identifier;
        if (upper == "AND") {
            type = TokenType::And;
        } else if (upper == "OR") {
            type = TokenType::Or;
        } else if (upper == "NOT") {
            type = TokenType::Not;
        } else if (upper == "IN") {
            type = TokenType::In;
        } else if (upper == "IS") {
            type = TokenType::Is;
        } else if (upper == "WAS") {
            type = TokenType::Was;
        } else if (upper == "CHANGED") {
            type = TokenType::Changed;
        } else if (upper == "ASC") {
            type = TokenType::Asc;
        } else if (upper == "DESC") {
            type = TokenType::Desc;
        } else if (upper == "EMPTY" || upper == "NULL") {
            type = TokenType::Empty;
        } else if (upper == "FROM") {
            type = TokenType::From;
        } else if (upper == "TO") {
            type = TokenType::To;
        } else if (upper == "BY") {
            type = TokenType::By;
        } else if (upper == "DURING") {
            type = TokenType::During;
        } else if (upper == "BEFORE") {
            type = TokenType::Before;
        } else if (upper == "AFTER") {
            type = TokenType::After;
        } else if (upper == "ON") {
            type = TokenType::On;
        }

        return {type, txt, txt, start_pos, len, start_line, start_col};
    }
};

} // namespace jql
