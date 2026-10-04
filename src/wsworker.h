#pragma once

#include <memory>
#include <string>
#include <map>
#include <fcgio.h>


namespace backend {

class router;

class wsworker
{
public:
    wsworker(std::shared_ptr<const router> router = nullptr);

    std::map<std::string, std::string> parse_request(std::shared_ptr<FCGX_Request> request);
    std::map<std::string, std::string> parse_args(const std::string &params);
    void process(std::shared_ptr<FCGX_Request> request);

    void set_router(std::shared_ptr<const router> router) { m_router = std::move(router); }
    std::shared_ptr<const router> get_router() const { return m_router; }

    const std::string& body() const { return m_body; }
    std::shared_ptr<FCGX_Request> request() const { return m_request; }
    FCGX_Stream* out() const { return m_request ? m_request->out : nullptr; }

private:
    std::shared_ptr<const router> m_router;
    std::shared_ptr<FCGX_Request> m_request;
    std::string m_body;
};

};
