#pragma once
#include <notstd/util/async/executor_traits.hpp>
#include <notstd/util/async/lowest_layer.hpp>
#include <notstd/util/async/tcp_socket_connect_state_impl.hpp>
#include <notstd/util/net.hpp>

namespace notstd::util::async
{
    template < class NextLayer >
    struct basic_ssl_stream_connect_state_impl
    : executor_traits< typename NextLayer::executor_type >
    {
        using traits_type =
            executor_traits< typename NextLayer::executor_type >;
        using stream_type   = net::ssl::stream< NextLayer >;
        using executor_type = typename traits_type::executor_type;

        using result_type = net::ip::tcp::endpoint;

        using awaitable =
            typename traits_type::template awaitable< result_type >;

        basic_ssl_stream_connect_state_impl(stream_type &stream)
        : stream_(stream)
        {
        }

        auto operator()(std::string const &host, std::string_view port)
            -> awaitable;

        auto cancel(error_code = net::error::operation_aborted) -> void;

        auto get_executor() -> executor_type { return stream_.get_executor(); }

        friend auto operator<<(std::ostream &                             os,
                               basic_ssl_stream_connect_state_impl const &state)
        -> std::ostream &
        {
            fmt::print(os, "[ssl_connect {}]", print(state.stream_.next_layer()));
            return os;
        }

      private:
        stream_type &                     stream_;
        std::function< void(error_code) > on_cancel_;
    };

    template < class NextLayer >
    auto make_connect_state_impl(net::ssl::stream< NextLayer > &stream)
    {
        return basic_ssl_stream_connect_state_impl< NextLayer >(stream);
    }

}   // namespace notstd::util::async

namespace notstd::util::async
{
    template < class NextLayer >
    auto basic_ssl_stream_connect_state_impl< NextLayer >::operator()(
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
        // connect next layer
        //

        auto next_connect_state = make_connect_state_impl(stream_.next_layer());
        on_cancel_              = [&](error_code ec) {
            my_error = ec;
            next_connect_state.cancel(ec);
        };
        auto ep = co_await next_connect_state(host, port);
        error_check();

        //
        // perform the SSL handshake
        //

        spdlog::trace("{} starting ssl handshake: {}", *this, host);
        if (not ::SSL_set_tlsext_host_name(stream_.native_handle(),
                                           host.c_str()))
        {
            throw system_error(
                error_code(::ERR_get_error(), net::error::ssl_category));
        }

        on_cancel_ = [&](error_code ec) {
            my_error = ec;
            spdlog::trace("{} cancel signal: {}", *this, ec);
            get_lowest_layer(stream_).cancel();
        };
        co_await(stream_.async_handshake(net::ssl::stream_base::client,
                                         this->use_awaitable));
        error_check();
        spdlog::trace("{} ssl up", *this);

        co_return ep;
    }
    catch (...)
    {
        on_cancel_ = nullptr;
        spdlog::trace("{} exception: {}", *this, explain());
        throw;
    }

    template < class NextLayer >
    auto basic_ssl_stream_connect_state_impl< NextLayer >::cancel(error_code ec)
        -> void
    {
        if (on_cancel_)
            on_cancel_(ec);
    }
}   // namespace notstd::util::async