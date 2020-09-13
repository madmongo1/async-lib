#pragma once
#include <notstd/util/async/executor_traits.hpp>
#include <notstd/util/net.hpp>
#include <notstd/util/wise_enum.hpp>

namespace notstd::util::async
{
    template < class Executor >
    struct tcp_resolve_state_impl : executor_traits< Executor >
    {
        using executor_type =
            typename executor_traits< Executor >::executor_type;

        using protocol_type = net::ip::tcp;
        using resolver_type =
            net::ip::basic_resolver< protocol_type, executor_type >;
        using results_type   = net::ip::basic_resolver_results< protocol_type >;
        using awaitable_type = typename executor_traits<
            Executor >::template awaitable< results_type >;

        tcp_resolve_state_impl(executor_type const &exec)
        : resolver_(exec)
        , on_cancel_(nullptr)
        {
        }

        auto operator()(std::string_view host, std::string_view port)
            -> awaitable_type;

        auto cancel(error_code ec = net::error::operation_aborted) -> void;

        auto get_executor() -> executor_type
        {
            return resolver_.get_executor();
        }

        static auto ident() -> std::string_view { return "[tcp_resolve]"; }

      private:
        resolver_type                     resolver_;
        std::function< void(error_code) > on_cancel_;
    };

}   // namespace notstd::util::async

namespace notstd::util::async
{
    template < class Executor >
    auto tcp_resolve_state_impl< Executor >::cancel(error_code ec) -> void
    {
        if (on_cancel_)
            on_cancel_(ec);
    }

    template < class Executor >
    auto tcp_resolve_state_impl< Executor >::operator()(std::string_view host,
                                                        std::string_view port)
        -> awaitable_type
    try
    {
        auto my_error    = error_code();
        this->on_cancel_ = [&](error_code ec) {
            assert(net::is_correct_thread(get_executor()));
            spdlog::trace("{} cancel requested: {}", ident(), print(ec));
            my_error = ec;
            resolver_.cancel();
        };
        spdlog::trace("{} resolving: [host {}], [port {}]", ident(), host, port);
        auto results =
            co_await resolver_.async_resolve(host, port, this->use_awaitable);
        spdlog::trace("{} resolved: [results {}]", ident(), print(results));
        if (my_error)
            throw system_error(my_error);
        on_cancel_ = nullptr;
        co_return results;
    }
    catch (...)
    {
        on_cancel_ = nullptr;
        throw;
    }

}   // namespace notstd::util::async