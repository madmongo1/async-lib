#pragma once
#include "remote_result.hpp"

#include <notstd/util/async/poly_handler.hpp>
#include <notstd/util/async/websocket_state_impl.hpp>
#include <notstd/util/json.hpp>
#include <notstd/util/json_rpc/remote_result.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace notstd::util::json_rpc
{
    /// Represents the state implementation of a json_rpc websocket connection
    /// @tparam NextLayer
    template < class NextLayer >
    struct websocket_state_impl
    {
        using stream_state_impl =
            util::async::websocket_state_impl< NextLayer, json::string >;
        using executor_type = typename stream_state_impl::executor_type;

        template < class... Args >
        websocket_state_impl(Args &&... args);

        template < class OnMethod >
        auto operator()(OnMethod on_method)
            -> net::awaitable< void, executor_type >;

        auto close(websocket::close_reason reason =
                       websocket::close_code::going_away) -> void;

        auto connect(std::string host, std::string port, std::string target)
            -> net::awaitable< void, executor_type >;

        template < BOOST_ASIO_COMPLETION_TOKEN_FOR(
            void(error_code, remote_result)) CallHandler >
        auto
        async_call(json::string method, json::value params, CallHandler &&token)
            -> BOOST_ASIO_INITFN_RESULT_TYPE(CallHandler,
                                             void(error_code, remote_result));

        auto get_executor() -> executor_type
        {
            return stream_state_.get_executor();
        }

      private:
        stream_state_impl stream_state_;

        //
        // requests
        //

        using call_handler =
            util::async::poly_handler< void(error_code, remote_result) >;

        std::int64_t                                     request_id_;
        std::unordered_map< std::int64_t, call_handler > call_handlers_;
    };
}   // namespace notstd::util::json_rpc

namespace notstd::util::json_rpc
{
    template < class NextLayer >
    template < class... Args >
    websocket_state_impl< NextLayer >::websocket_state_impl(Args &&... args)
    : stream_state_(std::forward< Args >(args)...)
    {
    }

    template < class NextLayer >
    template < class OnMethod >
    auto websocket_state_impl< NextLayer >::operator()(OnMethod on_method)
        -> net::awaitable< void, executor_type >
    {
        auto on_rx = [&](std::span< char > text) {
            auto jrx = json::parse(json::string_view(text.data(), text.size()));
            auto &ojrx = jrx.as_object();
            if (auto i = ojrx.find("method"); i != ojrx.end())
            {
                on_method(std::move(i->value().as_string()),
                          std::move(ojrx.at("params")));
            }
            else if (i = ojrx.find("id"); i != ojrx.end())
            {
                auto ihandler = call_handlers_.find(i->value().as_int64());
                if (ihandler != call_handlers_.end())
                {
                    if (i = ojrx.find("result"); i != ojrx.end())
                    {
                        ihandler->second.post_completion(
                            error_code(), remote_result(std::move(i->value())));
                    }
                    else if (i = ojrx.find("error"); i != ojrx.end())
                    {
                        ihandler->second.post_completion(
                            error_code(),
                            remote_result(
                                remote_failure(std::move(i->value()))));
                    }
                    else
                    {
                        ihandler->second.post_completion(
                            error::invalid_content,
                            remote_result(error_code(error::invalid_content)));
                    }
                    call_handlers_.erase(ihandler);
                }
                else
                {
                    spdlog::debug("json_rpc::websocket_state_impl: unexpected "
                                  "response: {}",
                                  std::string_view(text.data(), text.size()));
                }
            }
            else
            {
                spdlog::error(
                    "json_rpc::websocket_state_impl: invalid frame: {}",
                    std::string_view(text.data(), text.size()));
            }
        };
        co_await stream_state_(on_rx);
    }

    template < class NextLayer >
    auto
    websocket_state_impl< NextLayer >::close(websocket::close_reason reason)
        -> void
    {
        stream_state_.close(reason);
    }

    template < class NextLayer >
    auto websocket_state_impl< NextLayer >::connect(std::string host,
                                                    std::string port,
                                                    std::string target)
        -> net::awaitable< void, executor_type >
    {
        // note: no need to co_await, the caller will do this
        return stream_state_.connect(
            std::move(host), std::move(port), std::move(target));
    }

    template < class NextLayer >
    template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, remote_result))
                   CallHandler >
    auto websocket_state_impl< NextLayer >::async_call(json::string  method,
                                                       json::value   params,
                                                       CallHandler &&token)
        -> BOOST_ASIO_INITFN_RESULT_TYPE(CallHandler,
                                         void(error_code, remote_result))
    {
        return net::async_initiate< CallHandler,
                                    void(error_code, remote_result) >(
            [&](auto &&handler) {
                auto id = request_id_++;
                auto ib = call_handlers_.emplace(id, call_handler());
                auto i  = ib.first;
                i->second.emplace_with_guards(std::move(handler),
                                              this->get_executor());
                auto request  = json::value { { "jsonrpc", "2.0" },
                                             { "method", std::move(method) },
                                             { "params", std::move(params) },
                                             { "id", id } };
                auto do_error = [this, i](error_code ec) {
                    i->second.post_completion(ec, remote_result());
                    call_handlers_.erase(i);
                };
                try
                {
                    stream_state_.send_text(json::to_string(request));
                }
                catch (system_error &se)
                {
                    do_error(se.code());
                }
                catch (...)
                {
                    do_error(net::error::fault);
                }
            },
            token);
    }

}   // namespace notstd::util::json_rpc