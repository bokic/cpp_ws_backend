#pragma once

#include <memory>
#include <string>
#include <map>
#include <fcgio.h>


namespace backend {

class wsworker
{
public:
    std::map<std::string, std::string> parse_request(std::shared_ptr<FCGX_Request> request);
    std::map<std::string, std::string> parse_args(const std::string &params);
    void process(std::shared_ptr<FCGX_Request> request);

    const std::string& body() const { return m_body; }
    std::shared_ptr<FCGX_Request> request() const { return m_request; }
    FCGX_Stream* out() const { return m_request ? m_request->out : nullptr; }

private:
    std::shared_ptr<FCGX_Request> m_request;
    std::string m_body;
};

};
