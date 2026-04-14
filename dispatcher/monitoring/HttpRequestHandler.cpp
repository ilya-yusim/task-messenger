#include "HttpRequestHandler.hpp"

#include <sstream>

namespace monitoring {

bool HttpRequestHandler::parse_request_line(const std::string& raw_request, HttpRequestLine& out) {
    // HTTP headers are line-oriented; route decisions only need the first line.
    const auto line_end = raw_request.find("\r\n");
    if (line_end == std::string::npos) {
        return false;
    }

    std::istringstream line_stream(raw_request.substr(0, line_end));
    if (!(line_stream >> out.method >> out.path >> out.version)) {
        return false;
    }

    if (out.version.rfind("HTTP/", 0) != 0) {
        return false;
    }

    return true;
}

std::string HttpRequestHandler::make_json_response(int status_code, const std::string& body) {
    std::string response;
    response.reserve(body.size() + 256);
    response += "HTTP/1.1 ";
    response += std::to_string(status_code);
    response += " ";
    response += reason_phrase(status_code);
    response += "\r\n";
    response += "Content-Type: application/json\r\n";
    // Snapshot endpoint is polled; disable intermediary caching by default.
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: ";
    response += std::to_string(body.size());
    response += "\r\n\r\n";
    response += body;
    return response;
}

std::string HttpRequestHandler::make_text_response(int status_code, const std::string& body) {
    std::string response;
    response.reserve(body.size() + 256);
    response += "HTTP/1.1 ";
    response += std::to_string(status_code);
    response += " ";
    response += reason_phrase(status_code);
    response += "\r\n";
    response += "Content-Type: text/plain; charset=utf-8\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: ";
    response += std::to_string(body.size());
    response += "\r\n\r\n";
    response += body;
    return response;
}

std::string HttpRequestHandler::reason_phrase(int status_code) {
    // Keep phrase mapping minimal to match the small endpoint surface.
    switch (status_code) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    default:
        return "Error";
    }
}

} // namespace monitoring
