#pragma once

#include <notstd/util/net.hpp>

namespace notstd::util::async
{
    template < class Executor >
    struct executor_traits_base
    {
        using executor_type = Executor;

        template < class T >
        using awaitable = net::awaitable< T, executor_type >;

        using use_awaitable_t = net::use_awaitable_t< executor_type >;

        constexpr static auto use_awaitable = use_awaitable_t();
    };

    template < class Executor >
    struct executor_traits : executor_traits_base< Executor >
    {
        using executor_type       = Executor;
        using inner_executor_type = Executor;

        static auto new_executor(executor_type exec) { return exec; }

        static auto inner_executor(executor_type const &exec) -> executor_type
        {
            return exec;
        }
    };

    template < class InnerExecutor >
    struct executor_traits< net::strand< InnerExecutor > >
    : executor_traits_base< net::strand< InnerExecutor > >
    {
        using inner_executor_type = InnerExecutor;
        using executor_type       = net::strand< InnerExecutor >;

        static auto new_executor(inner_executor_type const &exec)
        {
            return net::make_strand(exec);
        }

        static auto new_executor(executor_type const &exec)
        {
            return net::make_strand(exec.get_inner_executor());
        }

        static auto inner_executor(executor_type const &exec)
            -> inner_executor_type
        {
            return exec.get_inner_executor();
        }
    };

}   // namespace notstd::util::async