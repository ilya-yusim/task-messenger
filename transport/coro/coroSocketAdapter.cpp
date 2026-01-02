/**
 * \file coroSocketAdapter.cpp
 * \brief Implementation details for `transport::CoroSocketAdapter`.
 * \details Implements the connect() convenience method that delegates to the wrapped socket's
 * blocking connect interface with poll-based connection establishment and timeout checks.
 */

#include "CoroSocketAdapter.hpp"
#include <system_error>

namespace transport {

void CoroSocketAdapter::connect(const std::string &host, int port, std::error_code& error) {
    if (!socket_) {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return;
    }
    
    socket_->connect(host, port, error);
}

} // namespace transport
 