#pragma once

#include <deque>
#include <notstd/util/async/poly_handler.hpp>
#include <notstd/util/net.hpp>

namespace notstd::util::async
{
    template < class Type, class Executor >
    struct queue_impl
    {
        using executor_type = Executor;

        queue_impl(executor_type exec)
        : exec_(exec)
        , queue_()
        , handler_()
        {
        }

        void cancel(error_code ec = net::error::operation_aborted)
        {
            if (handler_)
                handler_.post_completion(ec, Type());
        }

        auto get_executor() const -> executor_type { return exec_; }

        auto push(Type x) -> void;
        template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, Type))
                       PopHandler >
        auto async_pop(PopHandler &&token)
            -> BOOST_ASIO_INITFN_RESULT_TYPE(PopHandler,
                                             void(error_code, Type));

        executor_type                          exec_;
        std::deque< Type >                     queue_;
        poly_handler< void(error_code, Type) > handler_;
    };
}   // namespace notstd::util::async

namespace notstd::util::async
{
    template < class Type, class Executor >
    template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, Type))
                   PopHandler >
    auto queue_impl< Type, Executor >::async_pop(PopHandler &&token)
        -> BOOST_ASIO_INITFN_RESULT_TYPE(PopHandler, void(error_code, Type))
    {
        return net::async_initiate< PopHandler, void(error_code, Type) >(
            [this](auto &&handler) {
                if (queue_.empty())
                    handler_.emplace_with_guards(std::move(handler),
                                                 get_executor());
                else
                {
                    auto exec =
                        net::get_associated_executor(handler, get_executor());
                    net::defer(exec,
                               [handler = std::move(handler),
                                val     = std::move(queue_.front())]() mutable {
                                   handler(error_code(), std::move(val));
                               });
                }
            },
            token);
    }

    template < class Type, class Executor >
    auto queue_impl<Type, Executor>::push(Type x) -> void
    {
        if (handler_.has_value())
        {
            assert(queue_.empty());
            handler_.post_completion(error_code(), std::move(x));
        }
        else
        {
            queue_.push_back(std::move(x));
        }
    }


}   // namespace notstd::util::async