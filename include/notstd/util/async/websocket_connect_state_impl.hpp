#pragma once
#include <notstd/util/async/executor_traits.hpp>
#include <notstd/util/async/lowest_layer.hpp>
#include <notstd/util/async/ssl_stream_connect_state_impl.hpp>
#include <notstd/util/net.hpp>
#include <notstd/util/websocket.hpp>

namespace notstd::util::async
{
    template < class NextLayer >
    struct basic_websocket_connect_state_impl
    : executor_traits< typename NextLayer::executor_type >
    {
        using traits_type =
            executor_traits< typename NextLayer::executor_type >;

        using websock_type = websocket::stream< NextLayer >;

        using executor_type = typename traits_type::executor_type;

        using result_type = void;

        using awaitable = typename traits_type::template awaitable< void >;

        basic_websocket_connect_state_impl(websock_type &websock)
        : websock_(websock)
        {
        }

        auto operator()(std::string const &host,
                        std::string_view   port,
                        std::string_view   target) -> awaitable;

        auto cancel(error_code = net::error::operation_aborted) -> void;

        auto get_executor() -> executor_type { return websock_.get_executor(); }

        friend auto operator<<(std::ostream &                             os,
                               basic_websocket_connect_state_impl const &state)
        -> std::ostream &
        {
            fmt::print(os, "[websocket_connect {}]", print(state.websock_));
            return os;
        }

      private:
        websock_type &                    websock_;
        std::function< void(error_code) > on_cancel_;
    };

    template < class NextLayer >
    auto make_connect_state_impl(websocket::stream< NextLayer > &websock)
    {
        return basic_websocket_connect_state_impl< NextLayer >(websock);
    }

}   // namespace notstd::util::async

namespace notstd::util::async
{
    template < class NextLayer >
    auto basic_websocket_connect_state_impl< NextLayer >::operator()(
        std::string const &host,
        std::string_view   port,
        std::string_view   target) -> awaitable
    try
    {
#if !defined(NDEBUG)
        auto my_executor = co_await net::this_coro::executor;
        assert(get_executor() == my_executor);
#endif

        auto my_error    = error_code();
        auto error_check = [&] {
            on_cancel_ = nullptr;
            if (my_error)
                throw system_error(my_error);
        };

        //
        // connect the next layer
        //

        spdlog::trace("{} connect next layer: [host {}] [port {}]", *this, host, port);
        auto next_layer_connect =
            make_connect_state_impl(websock_.next_layer());
        on_cancel_ = [&](error_code ec) {
#if !defined(NDEBUG)
            assert(get_executor() == my_executor);
#endif
            spdlog::trace("{} cancel signal: ", *this, ec);
            my_error = ec;
            next_layer_connect.cancel(ec);
        };
        co_await next_layer_connect(host, port);
        error_check();

        //
        // perform handshake
        //

        spdlog::trace("{} websocket handshake: [host {}] [target {}]", *this, host, target);
        on_cancel_ = [&](error_code ec) {
#if !defined(NDEBUG)
            assert(get_executor() == my_executor);
#endif
            my_error = ec;
            get_lowest_layer(websock_).cancel();
            spdlog::trace("{} cancel signal: {}", *this, ec);
        };

        co_await websock_.async_handshake(
            host,
            beast::string_view(target.data(), target.size()),
            this->use_awaitable);
        error_check();

        spdlog::trace("{} websocket up", *this);

        co_return;
    }
    catch (...)
    {
        on_cancel_ = nullptr;
        spdlog::trace("{} exception: {}", *this, explain());
        throw;
    }

    template < class NextLayer >
    auto basic_websocket_connect_state_impl< NextLayer >::cancel(error_code ec)
        -> void
    {
        if (on_cancel_)
            on_cancel_(ec);
    }
}   // namespace notstd::util::async