#include "wsworker.h"

#include <sstream>
#include <string>
#include <string_view>
#include <cstring>
#include <tuple>
#include <regex>
#include <list>
#include <map>
#include <cctype>
#include <stdexcept>
#include <fcgio.h>

#include "wsroute.h"


static constexpr int MAX_CONTENT_LENGTH = 10 * 1024 * 1024; // 10 MB limit to prevent DoS via memory exhaustion

static std::string url_decode(const std::string &src)
{
    std::string ret;
    ret.reserve(src.size());

    for (size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '+')
        {
            ret += ' ';
        }
        else if (src[i] == '%' && i + 2 < src.size() &&
                 isxdigit(static_cast<unsigned char>(src[i + 1])) &&
                 isxdigit(static_cast<unsigned char>(src[i + 2])))
        {
            auto hex_to_int = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            char decoded = static_cast<char>((hex_to_int(src[i + 1]) << 4) | hex_to_int(src[i + 2]));
            ret += decoded;
            i += 2;
        }
        else
        {
            ret += src[i];
        }
    }
    return ret;
}

static int method_from_string(const std::string &method)
{
    if (method == "GET") return backend::GET;
    if (method == "POST") return backend::POST;
    if (method == "PUT") return backend::PUT;
    if (method == "PATCH") return backend::PATCH;
    if (method == "DELETE") return backend::DELETE;
    return 0;
}

backend::wsworker::wsworker(std::shared_ptr<const router> router)
    : m_router(std::move(router))
{
}

std::map<std::string, std::string> backend::wsworker::parse_request(std::shared_ptr<FCGX_Request> request)
{
    std::map<std::string, std::string> ret;
    if (!request || !request->envp)
        return ret;

    for (char **envp = request->envp; *envp; ++envp)
    {
        const char *s = *envp;
        const char *eq = std::strchr(s, '=');
        if (eq && eq != s)
        {
            ret.emplace(std::piecewise_construct,
                        std::forward_as_tuple(s, eq - s),
                        std::forward_as_tuple(eq + 1));
        }
    }

    return ret;
}

std::map<std::string, std::string> backend::wsworker::parse_args(const std::string &params)
{
    std::map<std::string, std::string> ret;
    if (params.empty())
        return ret;

    size_t start = 0;
    while (start < params.size())
    {
        size_t end = params.find('&', start);
        if (end == std::string::npos)
            end = params.size();

        if (end > start)
        {
            std::string_view item(params.data() + start, end - start);
            auto pos = item.find('=');
            if (pos != std::string_view::npos)
            {
                std::string key = url_decode(std::string(item.substr(0, pos)));
                if (!key.empty())
                {
                    ret.emplace(std::move(key), url_decode(std::string(item.substr(pos + 1))));
                }
            }
            else
            {
                std::string key = url_decode(std::string(item));
                if (!key.empty())
                {
                    ret.emplace(std::move(key), "");
                }
            }
        }
        start = end + 1;
    }

    return ret;
}

void backend::wsworker::process(std::shared_ptr<FCGX_Request> request)
{
    m_request = request;
    if (m_body.capacity() > 65536) {
        std::string().swap(m_body);
    } else {
        m_body.clear();
    }

    struct body_guard {
        std::string &b;
        ~body_guard() {
            if (b.capacity() > 65536) {
                std::string().swap(b);
            } else {
                b.clear();
            }
        }
    } guard{m_body};

    try {
        std::map<std::string, std::string> header = parse_request(request);

        if (header.count("CONTENT_LENGTH") > 0)
        {
            int contentLen = 0;
            try {
                contentLen = std::stoi(header["CONTENT_LENGTH"]);
            } catch (...) {
                FCGX_FPrintF(request->out, "Status: 400 Bad Request\r\n\r\nInvalid Content-Length");
                FCGX_Finish_r(request.get());
                return;
            }

            if (contentLen < 0)
            {
                FCGX_FPrintF(request->out, "Status: 400 Bad Request\r\n\r\nInvalid Content-Length");
                FCGX_Finish_r(request.get());
                return;
            }

            if (contentLen > MAX_CONTENT_LENGTH)
            {
                FCGX_FPrintF(request->out, "Status: 413 Payload Too Large\r\n\r\nPayload Too Large");
                FCGX_Finish_r(request.get());
                return;
            }

            if (contentLen > 0)
            {
                m_body.resize(static_cast<unsigned int>(contentLen));
                int total_read = 0;
                while (total_read < contentLen)
                {
                    int n = FCGX_GetStr(&m_body[total_read], contentLen - total_read, request->in);
                    if (n <= 0)
                        break;
                    total_read += n;
                }
                m_body.resize(static_cast<unsigned int>(total_read));
            }
        }

        const std::string &path = header["SCRIPT_NAME"];
        std::cmatch cm;

        if (path.empty())
        {
            throw std::runtime_error("System error. SCRIPT_NAME not found!");
        }

        int request_method = 0;
        if (header.count("REQUEST_METHOD") > 0)
        {
            request_method = method_from_string(header["REQUEST_METHOD"]);
        }

        bool uri_matched = false;

        if (m_router)
        {
            auto router_lock = m_router->lock_shared();

            for (const auto &mw : m_router->middlewares())
            {
                if (!mw(this, header))
                {
                    FCGX_Finish_r(request.get());
                    return;
                }
            }

            for(const auto &route: m_router->routes())
            {
                bool matched = false;
                if (route.is_exact)
                {
                    matched = (path == route.pattern_str);
                }
                else
                {
                    matched = std::regex_match(path.c_str(), cm, route.uri);
                }

                if (matched)
                {
                    uri_matched = true;

                    if ((route.method & request_method) == 0)
                    {
                        continue;
                    }

                    auto work = route.function;
                    std::list<std::string> uri_params;

                    if (work == nullptr)
                    {
                        throw std::runtime_error("Route function is nullptr");
                    }

                    if (!route.is_exact)
                    {
                        for (unsigned i=1; i < cm.size(); ++i) {
                            uri_params.push_back(cm[i]);
                        }
                    }

                    router_lock.unlock();

                    work(this, header, uri_params);

                    FCGX_Finish_r(request.get());

                    return;
                }
            }
        }

        if (uri_matched)
        {
            FCGX_FPrintF(request->out, "Status: 405 Method Not Allowed\r\n\r\nMethod Not Allowed");
            FCGX_Finish_r(request.get());
            return;
        }

        FCGX_FPrintF(request->out, "Status: 404 Not Found\r\n\r\nNot Found");
        FCGX_Finish_r(request.get());
        return;

    } catch (const std::exception &error) {
        if (request->err) {
            FCGX_FPrintF(request->err, "[error] Internal server error: %s\n", error.what());
        }
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nInternal Server Error");
    } catch (...) {
        if (request->err) {
            FCGX_FPrintF(request->err, "[error] Unknown internal server error\n");
        }
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nInternal Server Error");
    }

    FCGX_Finish_r(request.get());
}
