#pragma once
#include <notstd/util/error.hpp>

namespace notstd::util::json_rpc
{
    struct error
    {
        enum protocol_errors
        {
            /// Response is not JSON
            not_json = 1,

            /// Response JSON does not have required fields
            invalid_content = 2,

            /// result object is empty when trying to get value
            empty_result = 3,

            /// The remote result was not a failure but the calling context
            /// expected it to be

            unexpected_success = 4,

            /// Authentication failure
            authentication_failure = 5,
        };
    };

    struct protocol_error_category : error_category
    {
        std::string message(int ev) const override;
        const char *name() const noexcept override;
    };

    inline auto get_protocol_error_category() -> protocol_error_category const &
    {
        static const protocol_error_category cat;
        return cat;
    }

    inline auto make_error_code(error::protocol_errors code) -> error_code
    {
        return error_code(static_cast< int >(code),
                          get_protocol_error_category());
    }
}   // namespace notstd::util::json_rpc

POWERTRADE_DECLARE_ERROR_ENUM(
    notstd::util::json_rpc::error::protocol_errors)
