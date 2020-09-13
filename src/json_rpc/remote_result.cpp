#include <fmt/ostream.h>
#include <notstd/util/explain.hpp>
#include <notstd/util/json_rpc/remote_result.hpp>
#include <notstd/util/overloaded.hpp>
#include <notstd/util/print.hpp>

namespace notstd::util::json_rpc
{
    remote_result::remote_result(remote_failure fail)
    : impl_(std::move(fail))
    {
    }

    remote_result::remote_result(error_code ec)
    : impl_(ec)
    {
    }

    remote_result::remote_result(boost::json::value result)
    : impl_(std::move(result))
    {
    }

    auto remote_result::get() const -> boost::json::value const &
    {
        return visit(
            overloaded {
                [](error_code const &ec) -> boost::json::value const & {
                    throw system_error(ec);
                },
                [](remote_failure const &fail) -> boost::json::value const & {
                    throw(fail);
                },
                [](boost::json::value const &val)
                    -> boost::json::value const & { return val; } },
            as_variant());
    }

    auto remote_result::get() -> boost::json::value &
    {
        return visit(
            overloaded { [](error_code &ec) -> boost::json::value & {
                            throw system_error(ec);
                        },
                         [](remote_failure &fail) -> boost::json::value & {
                             throw(fail);
                         },
                         [](boost::json::value &val) -> boost::json::value & {
                             return val;
                         } },
            as_variant());
    }

    bool remote_result::is_error() const
    {
        return holds_alternative< error_code >(as_variant());
    }

    bool remote_result::is_result() const
    {
        return holds_alternative< json::value >(as_variant());
    }

    bool remote_result::is_remote_failure() const
    {
        return holds_alternative< remote_failure >(as_variant());
    }

    auto operator<<(std::ostream &os, remote_result const &arg)
        -> std::ostream &
    {
        visit(overloaded { [&os](error_code const &ec) {
                              fmt::print(os,
                                         "[remote_result {}]",
                                         notstd::util::print(ec));
                          },
                           [&os](remote_failure const &rf) {
                               fmt::print(os, "[remote_result {}]", rf);
                           },
                           [&os](json::value const &val) {
                               fmt::print(
                                   os, "[remote_result [result {}]]", val);
                           } },
              arg.as_variant());
        return os;
    }

    remote_failure &remote_result::get_remote_failure()
    {
        return visit(
            overloaded {
                [](error_code &ec) -> remote_failure & {
                    throw system_error(ec);
                },
                [](remote_failure &rf) -> remote_failure & { return rf; },
                [](json::value &) -> remote_failure & {
                    throw system_error(error::unexpected_success);
                } },
            as_variant());
    }

}   // namespace notstd::util::json_rpc