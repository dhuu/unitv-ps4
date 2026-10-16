#pragma once
#include <string>

namespace http {

struct Response {
    int status = 0;          // código HTTP (0 = falha de rede)
    std::string body;
    std::string error;
    bool ok() const { return status >= 200 && status < 300; }
};

bool init();
// GET síncrono (chamar fora da thread de UI). maxBytes limita o download (0 = sem limite).
Response get(const std::string& url, size_t maxBytes = 0, int timeoutSec = 25);

} // namespace http
