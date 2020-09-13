#pragma once
#include <fmt/ostream.h>
#include <notstd/util/async/executor_traits.hpp>
#include <notstd/util/async/lowest_layer.hpp>
#include <notstd/util/async/tcp_resolve_state_impl.hpp>
#include <notstd/util/net.hpp>
#include <notstd/util/explain.hpp>

namespace notstd::util::async
{
    template < class Executor >
    struct basic_tcp_socket_connect_state_impl : executor_traits< Executor >
    {
        using protocol_type = net::ip::tcp;
        using socket_type = net::basic_stream_socket< protocol_type, Executor >;
        using executor_type = Executor;

        using result_type = net::ip::tcp::endpoint;

        using awaitable = typename executor_traits<
            Executor >::template awaitable< result_type >;

        basic_tcp_socket_connect_state_impl(socket_type &sock)
        : sock_(sock)
        {
        }

        auto operator()(std::string const &host, std::string_view port)
            -> awaitable;

        auto cancel(error_code = net::error::operation_aborted) -> void;

        auto get_executor() -> executor_type { return sock_.get_executor(); }

        friend auto operator<<(std::ostream &                             os,
                               basic_tcp_socket_connect_state_impl const &state)
            -> std::ostream &
        {
            fmt::print(os, "[tcp_connect {}]", print(state.sock_));
            return os;
        }

      private:
        socket_type &                     sock_;
        std::function< void(error_code) > on_cancel_;
    };

    template < class Executor >
    auto make_connect_state_impl(
        net::basic_stream_socket< net::ip::tcp, Executor > &sock)
    {
        return basic_tcp_socket_connect_state_impl< Executor >(sock);
    }

}   // namespace notstd::util::async

namespace notstd::util::async
{
    template < class Executor >
    auto basic_tcp_socket_connect_state_impl< Executor >::operator()(
        std::string const &host,
        std::string_view   port) -> awaitable
    try
    {
        assert(get_executor() == co_await net::this_coro::executor);

        auto my_error    = error_code();
        auto error_check = [&] {
            on_cancel_ = nullptr;
            if (my_error)
                throw system_error(my_error);
        };

        //
        // resolve the address
        //

        auto resolve_state =
            tcp_resolve_state_impl< executor_type >(sock_.get_executor());
        on_cancel_ = [&](error_code ec) {
            spdlog::trace("{} cancel request: {}", *this, ec);
            my_error = ec;
            resolve_state.cancel(ec);
        };
        auto endpoints = co_await resolve_state(host, port);
        error_check();

        //
        // connect the socket
        //

        on_cancel_ = [&](error_code ec) {
            my_error = ec;
            sock_.cancel();
        };
        auto ep =
            co_await net::async_connect(sock_, endpoints, this->use_awaitable);
        error_check();
        spdlog::trace("{} connected: {}", *this, ep);

        co_return ep;
    }
    catch (...)
    {
        on_cancel_ = nullptr;
        spdlog::debug("{} exception: {}", *this, explain());
        throw;
    }

    template < class Executor >
    auto basic_tcp_socket_connect_state_impl< Executor >::cancel(error_code ec)
        -> void
    {
        if (on_cancel_)
            on_cancel_(ec);
    }
}   // namespace notstd::util::async