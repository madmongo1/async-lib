#pragma once
#include <notstd/util/async/cheap_work_guard.hpp>

namespace notstd::util::async
{
    template < class Handler, class... GuardedExecutors >
    struct basic_handler_with_work_guard
    {
        using executor_type = typename Handler::executor_type;

        template < class HandlerArg >
        basic_handler_with_work_guard(HandlerArg &&handler,
                                      GuardedExecutors const &... execs)
        : handler_(std::forward< HandlerArg >(handler))
        , guards_(make_cheap_work_guard(execs)...)
        {
        }

        template < class... Args >
        auto operator()(Args &&... args) -> decltype(auto)
        {
            handler_(std::forward< Args >(args)...);
        }

        auto get_executor() const -> executor_type
        {
            return handler_.get_executor();
        }

      private:
        Handler                                                    handler_;
        std::tuple< cheapest_work_guard_t< GuardedExecutors >... > guards_;
    };

    template < class Handler, class... GuardedExecutors >
    auto wrap_work_guard(Handler &&handler, GuardedExecutors const &... execs)
    {
        using result_type =
            basic_handler_with_work_guard< std::decay_t< Handler >,
                                           GuardedExecutors... >;
        return result_type(std::move(handler), execs...);
    }
}   // namespace notstd::util::async