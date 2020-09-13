#include <boost/json/serializer.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <notstd/util/json_rpc/remote_failure.hpp>

namespace notstd::util::json_rpc
{
    remote_failure::remote_failure(boost::json::value error,
                                   std::string        context)
    : context_(std::move(context))
    , error_(std::move(error))
    {
    }

    const char *remote_failure::what() const noexcept
    {
        static thread_local std::string buffer = "remote_failure";
        try
        {
            buffer = fmt::format(
                "[remote_failure [context {}] [error {}]]", context_, error_);
        }
        catch (...)
        {
        }
        return buffer.c_str();
    }

    std::string remote_failure::build_message(std::string const &       context,
                                              boost::json::value const &error)
    {
        auto result = fmt::format(
            "json_rpc error {}: {}",
            context.empty() ? "" : fmt::format("while {} ", context),
            error);
        return result;
    }

    auto operator<<(std::ostream &os, remote_failure const &arg)
        -> std::ostream &
    {
        fmt::print(os,
                   "[remote_failure [context {}] [error {}]]",
                   arg.context_,
                   arg.error_);
        return os;
    }

}   // namespace notstd::util::json_rpc