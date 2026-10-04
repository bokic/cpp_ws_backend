#include "wsworker.h"

#include <sstream>
#include <string>
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
        else if (src[i] == '%' && i + 2 < src.size() && isxdigit(src[i + 1]) && isxdigit(src[i + 2]))
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

    for(char **envp = request->envp; *envp; envp++)
    {
        std::string item(*envp);

        auto i = item.find('=');
        if (i > 0)
        {
            std::string key = item.substr(0, i);
            std::string val = item.substr(i + 1);

            ret.insert(std::make_pair(key, val));
        }
    }

    return ret;
}

std::map<std::string, std::string> backend::wsworker::parse_args(const std::string &params)
{
    std::map<std::string, std::string> ret;

    std::istringstream f(params);
    std::string item;
    while (std::getline(f, item, '&')) {
        if (item.empty())
            continue;

        auto pos = item.find('=');
        std::string key;
        std::string val;

        if (pos != std::string::npos)
        {
            key = url_decode(item.substr(0, pos));
            val = url_decode(item.substr(pos + 1));
        }
        else
        {
            key = url_decode(item);
            val = "";
        }

        if (!key.empty())
        {
            ret[key] = val;
        }
    }

    return ret;
}

void backend::wsworker::process(std::shared_ptr<FCGX_Request> request)
{
    m_request = request;
    m_body.clear();

    try {
        std::map<std::string, std::string> header;

        header = parse_request(request);

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
                FCGX_GetStr(&m_body.at(0), contentLen, request->in);
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
            for(const auto &route: m_router->routes())
            {
                if (std::regex_match(path.c_str(), cm, route.uri))
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

                    for (unsigned i=1; i < cm.size(); ++i) {
                        uri_params.push_back(cm[i]);
                    }

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
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nInternal error: %s", error.what());
    } catch (...) {
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nUnknown internal error!!!");
    }

    FCGX_Finish_r(request.get());
}
