#include "wsworker.h"

#include <sstream>
#include <string>
#include <regex>
#include <list>
#include <map>
#include <fcgio.h>

#include "webapp.cpp"


using namespace std;

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

    try {
        map<string, string> header;
        string request_body;

        header = parse_request(request);

        if (header.count("CONTENT_LENGTH") > 0)
        {
            int contentLen = 0;
            try {
                contentLen = stoi(header["CONTENT_LENGTH"]);
            } catch (...) {}

            if (contentLen > 0)
            {
                request_body.resize(static_cast<unsigned int>(contentLen));
                FCGX_GetStr(&request_body.at(0), contentLen, request->in);
            }
        }

        const string &path = header["SCRIPT_NAME"];
        std::cmatch cm;

        if (path.empty())
        {
            throw string("System error. SCRIPT_NAME not found!");
        }

        for(const auto &route: routeMap)
        {
            if (std::regex_match(path.c_str(), cm, route.uri))
            {
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

        throw string("Route not found!");

    } catch (string &error) {
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nInternal error: %s", error.c_str());
    } catch (...) {
        FCGX_FPrintF(request->out, "Status: 500 Internal Server Error\r\n\r\nUnknown internal error!!!");
    }

    FCGX_Finish_r(request.get());
}
