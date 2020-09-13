#pragma once
#include <boost/json/value.hpp>
#include <stdexcept>
#include <string>
#include <optional>

namespace notstd::util::json_rpc
{
    struct remote_failure : std::exception
    {
        remote_failure(boost::json::value error,
                       std::string        context = std::string());

        std::string const &       context() const noexcept { return context_; }
        boost::json::value const &error() const noexcept { return error_; }
        boost::json::value &      error() noexcept { return error_; }

        const char *what() const noexcept;

      private:
        static std::string build_message(std::string const &       context,
                                         boost::json::value const &error);

        friend auto operator<<(std::ostream &os, remote_failure const &arg)
            -> std::ostream &;

      private:
        std::string                  context_;
        boost::json::value           error_;
        std::optional< std::string > what_string_;
    };
}   // namespace notstd::util::json_rpc