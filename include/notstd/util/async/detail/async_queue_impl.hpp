#pragma once
#include <boost/mp11/tuple.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <deque>
#include <notstd/util/async/cheap_work_guard.hpp>
#include <notstd/util/async/detail/has_get_executor.hpp>
#include <notstd/util/async/poly_handler.hpp>
#include <notstd/util/net.hpp>
#include <queue>

namespace notstd::util::async::detail
{
    template < class H, class... OtherExecutors >
    struct guarded_handler
    {
        using executor_type = net::associated_executor_t< H >;

        explicit guarded_handler(H &&h, OtherExecutors... others)
        : handler_(std::move(h))
        , guards_(handler_.get_executor(), others...)
        {
        }

        template < class... Args >
        void operator()(Args &&... args)
        {
            boost::mp11::tuple_for_each(guards_, [](auto &&g) { g.reset(); });
            handler_(std::forward< Args >(args)...);
        }

        executor_type get_executor() const
        {
            return net::get_associated_executor(handler_);
        }

      private:
        H handler_;
        std::tuple< cheapest_work_guard_t< typename H::executor_type >,
                    cheapest_work_guard_t< OtherExecutors >... >
            guards_;
    };

    template < class T, class Executor >
    struct async_queue_impl
    : boost::intrusive_ref_counter< async_queue_impl< T, Executor > >
    {
        using value_type    = T;
        using executor_type = Executor;
        using ptr           = boost::intrusive_ptr< async_queue_impl >;

        enum waiting_state
        {
            not_waiting,
            waiting
        };

        async_queue_impl(executor_type exec)
        : state_(not_waiting)
        , handler_()
        , values_()
        , default_executor_(exec)
        {
        }

        template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, value_type))
                       WaitHandler >
        BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(WaitHandler,
                                           void(error_code, value_type))
        async_pop(WaitHandler &&handler);

        static ptr construct(executor_type exec);

        void push(value_type v);

        void stop();

      private:
        void maybe_complete();

      private:
        std::atomic< waiting_state > state_ = not_waiting;
        util::async::poly_handler< void(error_code, value_type) > handler_;

        using queue_impl = std::queue< T, std::deque< T > >;
        queue_impl values_;
        error_code ec_;   // error state of the queue

        // accessed by both internal and external threads
        executor_type default_executor_;
    };
}   // namespace notstd::util::async::detail

namespace notstd::util::async::detail
{
    template < class T, class Executor >
    template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, value_type))
                   WaitHandler >
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(WaitHandler,
                                       void(error_code, value_type))
    async_queue_impl< T, Executor >::async_pop(WaitHandler &&handler)
    {
        assert(this->state_ == not_waiting);

        auto initiate = [this](auto &&deduced_handler) {
            using DeducedHandler = decltype(deduced_handler);

            if constexpr (has_get_executor_v< DeducedHandler >)
                if (deduced_handler.get_executor() == this->default_executor_)
                    this->handler_ = guarded_handler(
                        std::forward< DeducedHandler >(deduced_handler));
                else
                    this->handler_ = guarded_handler(
                        std::forward< DeducedHandler >(deduced_handler),
                        this->default_executor_);

            else
                this->handler_ = guarded_handler(net::bind_executor(
                    this->default_executor_,
                    std::forward< DeducedHandler >(deduced_handler)));

            this->state_ = waiting;

            net::post(net::bind_executor(this->default_executor_,
                                         [self = boost::intrusive_ptr(this)]() {
                                             self->maybe_complete();
                                         }));
        };

        return net::async_initiate< WaitHandler, void(error_code, value_type) >(
            initiate, handler);
    }

    template < class T, class Executor >
    auto async_queue_impl< T, Executor >::construct(executor_type exec) -> ptr
    {
        return ptr(new async_queue_impl(exec));
    }

    template < class T, class Executor >
    void async_queue_impl< T, Executor >::push(value_type v)
    {
        net::post(net::bind_executor(
            this->default_executor_,
            [self = boost::intrusive_ptr(this), v = std::move(v)]() mutable {
                self->values_.push(std::move(v));
                self->maybe_complete();
            }));
    }

    template < class T, class Executor >
    void async_queue_impl< T, Executor >::maybe_complete()
    {
        // running in default executor...
        if (values_.empty() and not ec_)
            return;
        if (state_.exchange(not_waiting) != waiting)
            return;

        auto e = this->handler_.get_executor();

        if (ec_)
        {
            auto op = [ec   = std::exchange(ec_, {}),
                       self = boost::intrusive_ptr(this)] {
                self->handler_(ec, value_type());
            };
            if (e == this->default_executor_)
            {
                op();
            }
            else
            {
                net::post(net::bind_executor(e, op));
            }
        }
        else
        {
            auto v = std::move(values_.front());
            values_.pop();

            auto op = [self = boost::intrusive_ptr(this),
                       v    = std::move(v)]() mutable {
                self->handler_(error_code(), std::move(v));
            };

            if (e == default_executor_)
                op();
            else
                net::post(net::bind_executor(e, op));
        }
    }

    template < class T, class Executor >
    void async_queue_impl< T, Executor >::stop()
    {
        net::post(
            net::bind_executor(this->default_executor_,
                               [self = boost::intrusive_ptr(this)]() mutable {
                                   self->ec_ = net::error::operation_aborted;
                                   while (not self->values_.empty())
                                       self->values_.pop();
                                   self->maybe_complete();
                               }));
    }

}   // namespace notstd::util::async::detail
