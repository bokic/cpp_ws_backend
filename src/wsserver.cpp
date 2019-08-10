#include "wsserver.h"
#include "router.h"

#include <sstream>
#include <string>
#include <list>
#include <map>

#include <fcgio.h>


using namespace std;

map<string, string> backend::parse_args(const string &params)
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

static void do_request(map<string, string> header)
{
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
            void (*work)(map<string, string>, list<string>) = route.function;
            list<string> uri_params;

            if (work == nullptr)
            {
                throw string("Route function is nullptr");
            }

            for (unsigned i=1; i < cm.size(); ++i) {
                uri_params.push_back(cm[i]);
            }

            work(header, uri_params);

            return;
        }
    }

    throw string("Route not found!");
}

backend::wsserver::wsserver()
{
    // Backup the stdio streambufs
    m_cin_streambuf  = cin.rdbuf();
    m_cout_streambuf = cout.rdbuf();
    //m_cerr_streambuf = cerr.rdbuf();

    FCGX_Init();
}

backend::wsserver::~wsserver()
{
    // restore stdio streambufs
    cin.rdbuf(m_cin_streambuf);
    cout.rdbuf(m_cout_streambuf);
    //cerr.rdbuf(m_cerr_streambuf);
}

void backend::wsserver::init(const char *socket_name, int backlog)
{
    m_sock_fd = FCGX_OpenSocket(socket_name, backlog);
}

int backend::wsserver::run()
{
    FCGX_Request request;

    FCGX_InitRequest(&request, m_sock_fd, 0);

    // TODO: Make following block multithreaded with thread pool.
    while (FCGX_Accept_r(&request) == 0) {
        fcgi_streambuf cin_fcgi_streambuf(request.in);
        fcgi_streambuf cout_fcgi_streambuf(request.out);
        fcgi_streambuf cerr_fcgi_streambuf(request.err);
        map<string, string> header;
        string request_body;

        cin.rdbuf(&cin_fcgi_streambuf);
        cout.rdbuf(&cout_fcgi_streambuf);
        //cerr.rdbuf(&cerr_fcgi_streambuf);

        try {

            for(char **envp = request.envp; *envp; envp++)
            {
                string item(*envp);

                auto i = item.find('=');
                if (i > 0)
                {
                    string key = item.substr(0, i);
                    string val = item.substr(i + 1);

                    header.insert(make_pair(key, val));
                }
            }

            cin >> request_body;

            do_request(header);

        } catch (const string &err) {
            cout << "\r\n";
            cout << err;
        } catch (...) {
            cout << "\r\n";
            cout << "Unknown error!!!";
        }

        // Note: the fcgi_streambuf destructor will auto flush
    }

    return 0;
}
