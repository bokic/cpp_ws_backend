#include "wsworker.h"

#include <sstream>
#include <string>
#include <regex>
#include <list>
#include <map>
#include <fcgio.h>

#include "wsroute.h"
#include "webapp.h"


using namespace std;

static constexpr int MAX_CONTENT_LENGTH = 10 * 1024 * 1024; // 10 MB limit to prevent DoS via memory exhaustion

static int method_from_string(const string &method)
{
    if (method == "GET") return backend::GET;
    if (method == "POST") return backend::POST;
    if (method == "PUT") return backend::PUT;
    if (method == "PATCH") return backend::PATCH;
    if (method == "DELETE") return backend::DELETE;
    return 0;
}

map<string, string> backend::wsworker::parse_request(std::shared_ptr<FCGX_Request> request)
{
    map<string, string> ret;

    for(char **envp = request->envp; *envp; envp++)
    {
        string item(*envp);

        auto i = item.find('=');
        if (i > 0)
        {
            string key = item.substr(0, i);
            string val = item.substr(i + 1);

            ret.insert(make_pair(key, val));
        }
    }

    return ret;
}

map<string, string> backend::wsworker::parse_args(const string &params)
{
    map<string, string> ret;

    istringstream f(params);
    string item;
    while (getline(f, item, '&')) {

        auto pos = item.find('=');

        const auto &key = item.substr(0, pos);
        const auto &val = item.substr(pos + 1);

        ret.insert(make_pair<string, string>(key.data(), val.data()));
    }

    return ret;
}

void backend::wsworker::process(std::shared_ptr<FCGX_Request> request)
{
    m_request = request;
    m_body.clear();

    try {
        map<string, string> header;

        header = parse_request(request);

        if (header.count("CONTENT_LENGTH") > 0)
        {
            int contentLen = 0;
            try {
                contentLen = stoi(header["CONTENT_LENGTH"]);
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

        const string &path = header["SCRIPT_NAME"];
        std::cmatch cm;

        if (path.empty())
        {
            throw string("System error. SCRIPT_NAME not found!");
        }

        int request_method = 0;
        if (header.count("REQUEST_METHOD") > 0)
        {
            request_method = method_from_string(header["REQUEST_METHOD"]);
        }

        bool uri_matched = false;

        for(const auto &route: routeMap)
        {
            if (std::regex_match(path.c_str(), cm, route.uri))
            {
                uri_matched = true;

                if ((route.method & request_method) == 0)
                {
                    continue;
                }

                void (*work)(wsworker *worker, map<string, string>, list<string>) = route.function;
                list<string> uri_params;

                if (work == nullptr)
                {
                    throw string("Route function is nullptr");
                }

                for (unsigned i=1; i < cm.size(); ++i) {
                    uri_params.push_back(cm[i]);
                }

                work(this, header, uri_params);

                FCGX_Finish_r(request.get());

                return;
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
    } catch (const string &error) {
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nInternal error: %s", error.c_str());
    } catch (...) {
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nUnknown internal error!!!");
    }

    FCGX_Finish_r(request.get());
}
