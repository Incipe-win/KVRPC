#pragma once
#include <stdexcept>
#include <cstdint>
#include <string>

namespace kvrpc {
enum class ErrorCode { invalid_argument, overloaded, closed, timeout, connection, transport, protocol, remote };
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};
class RemoteError : public Error {
public:
    RemoteError(uint32_t status, const std::string& message) : Error(ErrorCode::remote, message), status_(status) {}
    uint32_t status() const noexcept { return status_; }
private:
    uint32_t status_;
};
}  // namespace kvrpc
