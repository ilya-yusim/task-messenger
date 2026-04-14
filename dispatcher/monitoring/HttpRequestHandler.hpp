#pragma once

#include <string>

namespace monitoring {

/** \brief Parsed HTTP request line components used by the minimal router. */
struct HttpRequestLine {
    std::string method;
    std::string path;
    std::string version;
};

/**
 * \brief Utility for minimal HTTP/1.1 parsing and response formatting.
 *
 * Part B intentionally keeps this lightweight (GET-only routing for monitoring).
 */
class HttpRequestHandler {
public:
    /** \brief Parse first request line: METHOD PATH HTTP/x.y. */
    static bool parse_request_line(const std::string& raw_request, HttpRequestLine& out);

    /** \brief Build a JSON response with Content-Length and close semantics. */
    static std::string make_json_response(int status_code, const std::string& body);

    /** \brief Build a plain-text response (used for health/errors). */
    static std::string make_text_response(int status_code, const std::string& body);

private:
    static std::string reason_phrase(int status_code);
};

} // namespace monitoring
