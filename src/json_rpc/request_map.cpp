#include <boost/json/parser.hpp>
#include <notstd/util/json_rpc/error.hpp>
#include <notstd/util/json_rpc/request_map.hpp>
#include <spdlog/spdlog.h>
#include <fmt/ostream.h>

namespace notstd::util::json_rpc
{
    request_map::request_map()
    : outstanding_()
    , pending_authentication_()
    , current_id_(0)
    , auth_state_(not_authenticated)
    {
    }

    auto request_map::async_complete(json::value jframe) -> void
    {
        json::object &resp = jframe.as_object();
        auto          id   = resp.at("id").as_int64();

        // from this point on, we can respond to the outstanding request

        auto ec     = error_code();
        auto result = remote_result();
        if (auto i = resp.find("result"); i != resp.end())
        {
            result.assign(std::move(i->value()));
        }
        else if (i = resp.find("error"); i != resp.end())
        {
            result.assign(remote_failure(std::move(i->value())));
        }
        else
        {
            ec = error::invalid_content;
        }

        assert(ec or not result.is_error());

        auto i = outstanding_.find(id);
        if (i != outstanding_.end())
        {
            auto &handler = i->second;
            handler.post_completion(ec, std::move(result));
            outstanding_.erase(i);
        }
        else
        {
            spdlog::debug(
                "request_map::async_complete : unmatched response [{}]",
                jframe);
        }
    }

    auto request_map::cancel(error_code ec) -> void
    {
        auto cpy2 = std::move(pending_authentication_);
        for (auto &r : cpy2)
            r.handler.post_completion(ec, remote_result(ec));

        auto cpy = std::move(outstanding_);
        for (auto &[key, handler] : cpy)
            handler.post_completion(ec, remote_result(ec));
    }

}   // namespace notstd::util::json_rpc