#include <fmt/format.h>
#include <notstd/util/json_rpc/error.hpp>

namespace notstd::util::json_rpc
{
    const char *protocol_error_category::name() const noexcept
    {
        return "json_rpc::protocol_error";
    }

    std::string protocol_error_category::message(int ev) const
    {
        switch (static_cast< error::protocol_errors >(ev))
        {
        case error::not_json:
            return "Not JSON.";

        case error::invalid_content:
            return "Invalid or missing content.";

        case error::empty_result:
            return "Empty result.";

        case error::unexpected_success:
            return "Unexpected success.";

        case error::authentication_failure:
            return "Authentication failure.";
        }

        return fmt::format("Unknown code: {}.", ev);
    }
}   // namespace notstd::util::json_rpc