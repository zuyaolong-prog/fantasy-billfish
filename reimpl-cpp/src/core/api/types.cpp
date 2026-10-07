#include "core/api/types.hpp"

namespace bf::core::api {

ResponseInfo make_ok(std::string message) {
    return ResponseInfo{err::Ok, std::move(message)};
}

ResponseInfo make_err(const std::int32_t code, std::string message) {
    return ResponseInfo{code, std::move(message)};
}

} // namespace bf::core::api
