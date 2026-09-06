#pragma once
#include <stdexcept>
#include <string>

namespace kvrpc {
enum class ErrorCode { invalid_argument, overloaded, closed, timeout, connection, transport, protocol };
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};
}  // namespace kvrpc
