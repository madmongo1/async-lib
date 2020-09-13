#pragma once
#include <mutex>
#include <optional>
#include <notstd/util/async/cheap_work_guard.hpp>
#include <notstd/util/async/poly_handler.hpp>
#include <tuple>

namespace notstd::util::async
{
    struct async_join_impl_base
    {
        template < class Tuple, std::size_t... Is >
        static auto transform_to_tie(Tuple &&t, std::index_sequence< Is... >)
        {
            return std::tie(
                std::get< Is >(std::forward< Tuple >(t)).value()...);
        }
    };

    template < class Executor = net::executor, class... Events >
    struct async_join_impl : async_join_impl_base
    {
        using executor_type = Executor;

        template < class OtherExecutor >
        struct rebind_executor
        {
            using other = async_join_impl< OtherExecutor, Events... >;
        };

        auto get_executor() const -> executor_type { return exec_; }

        async_join_impl(executor_type exec);

        template <
            BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler
                BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(error_code) >
        auto async_wait(CompletionHandler &&token
                            BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
            -> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler,
                                             void(error_code));

        /// Cancel any outstanding wait operation and move the impl to the error
        /// state
        auto cancel(error_code ec = net::error::operation_aborted) -> void;

        template < class Event >
        auto set_event(Event e) -> void;

        template < class Event >
        auto unset_event() -> void;

        /// Must not be called unless the complation handler has been invoked
        /// @return
        auto events() -> std::tuple< Events &... >
        {
            assert(triggered());
            return transform_to_tie(
                events_, std::make_index_sequence< sizeof...(Events) >());
        }

        bool triggered() const;

      private:
        using event_tuple = std::tuple< std::optional< Events >... >;

        template < class Handler, class... Guards >
        struct wait_op_model
        {
            using executor_type = net::associated_executor_t< Handler >;

            wait_op_model(Handler handler, Guards... guards)
            : handler_(std::move(handler))
            , guards_(std::move(guards)...)
            {
            }

            auto get_executor() const
            {
                return net::get_associated_executor(handler_);
            }

            void operator()(error_code ec) { handler_(ec); }

            Handler                 handler_;
            std::tuple< Guards... > guards_;
        };

      private:
        template < class Handler, class... Guards >
        auto construct_wait_op(Handler &&handler, Guards &&... guards)
        {
            return wait_op_model< std::decay_t< Handler >,
                                  std::decay_t< Guards >... >(
                std::forward< Handler >(handler),
                std::forward< Guards >(guards)...);
        }

      private:
        Executor exec_;

        std::mutex                               mutex_;
        std::tuple< std::optional< Events >... > events_;
        poly_handler< void(error_code) >         handler_;
        error_code                               error_;
        enum state_code
        {
            not_waiting,
            waiting,
            complete,
            error
        } state_ = not_waiting;
    };

    template < class Executor = net::io_executor >
    struct async_event
    {
        using executor_type = Executor;

        template < class OtherExecutor >
        struct rebind_executor
        {
            using other = async_event< OtherExecutor >;
        };

        auto get_executor() -> executor_type { return exec_; }

        async_event(executor_type exec)
        : exec_(exec)
        {
        }

        template <
            BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler
                BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(error_code) >
        auto async_wait(CompletionHandler &&token
                            BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
            -> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler,
                                             void(error_code));

        /// Cancel any outstanding wait operation and move the impl to the error
        /// state
        auto cancel(error_code ec = net::error::operation_aborted) -> void
        {
            auto l = std::unique_lock(mutex_);
            error_ = ec;
            if (handler_.has_value())
                handler_.post_completion(error_);
        }

        auto set_event() -> void
        {
            auto l = std::unique_lock(mutex_);
            assert(not event_set_);
            event_set_ = true;
            if (handler_.has_value())
            {
                assert(not error_);
                handler_.post_completion(error_);
            }
        }

      private:
        template < class Handler, class... Guards >
        struct wait_op_model
        {
            using executor_type = net::associated_executor_t< Handler >;

            wait_op_model(Handler handler, Guards... guards)
            : handler_(std::move(handler))
            , guards_(std::move(guards)...)
            {
            }

            auto get_executor() const
            {
                return net::get_associated_executor(handler_);
            }

            void operator()(error_code ec) { handler_(ec); }

            Handler                 handler_;
            std::tuple< Guards... > guards_;
        };

      private:
        template < class Handler, class... Guards >
        auto construct_wait_op(Handler &&handler, Guards &&... guards)
        {
            return wait_op_model< std::decay_t< Handler >,
                                  std::decay_t< Guards >... >(
                std::forward< Handler >(handler),
                std::forward< Guards >(guards)...);
        }

      private:
        Executor exec_;

        std::mutex                       mutex_;
        bool                             event_set_ = false;
        poly_handler< void(error_code) > handler_;
        error_code                       error_;
    };

}   // namespace notstd::util::async

#include <boost/mp11/tuple.hpp>
#include <notstd/util/async/detail/has_get_executor.hpp>

namespace notstd::util::async
{
    template < class Executor, class... Events >
    async_join_impl< Executor, Events... >::async_join_impl(executor_type exec)
    : exec_(std::move(exec))
    , events_()
    {
    }

    template < class Executor, class... Events >
    template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code))
                   CompletionHandler >
    auto async_join_impl< Executor, Events... >::async_wait(
        CompletionHandler &&token)
        -> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler, void(error_code))
    {
        auto initiate = [this](auto &&handler) {
            assert(not handler_.has_value());

            auto exec = net::get_associated_executor(handler, get_executor());

            auto l = std::unique_lock(mutex_);
            switch (state_)
            {
            case waiting:
                assert(!"logic error");
                l.unlock();
                net::post(exec, [handler = std::move(handler)]() mutable {
                    handler(net::error::operation_not_supported);
                });
                break;

            case not_waiting:
                if (triggered())
                {
                    l.unlock();
                    net::post(exec, [handler = std::move(handler)]() mutable {
                        handler(error_code());
                    });
                }
                else
                {
                    if (exec == get_executor())
                    {
                        // only need a work guard on one executor
                        if constexpr (detail::has_get_executor_v< decltype(
                                          handler) >)
                            handler_ =
                                construct_wait_op(std::move(handler),
                                                  make_cheap_work_guard(exec));
                        else
                            handler_ = construct_wait_op(
                                net::bind_executor(exec, std::move(handler)),
                                make_cheap_work_guard(exec));
                    }
                    else
                    {
                        handler_ = construct_wait_op(
                            std::move(handler),
                            make_cheap_work_guard(exec),
                            make_cheap_work_guard(get_executor()));
                    }
                    state_ = waiting;
                }
                break;
            case complete:
                l.unlock();
                net::post(exec, [handler = std::move(handler)]() mutable {
                    handler(error_code());
                });
                break;

            case error:
                net::post(exec,
                          [handler = std::move(handler),
                           ec      = error_]() mutable { handler(ec); });
                break;
            }
        };

        return net::async_initiate< CompletionHandler, void(error_code) >(
            initiate, token);
    }

    template < class Executor, class... Events >
    auto async_join_impl< Executor, Events... >::triggered() const -> bool
    {
//        assert(net::is_correct_thread(get_executor()));
        bool r = true;
        boost::mp11::tuple_for_each(events_,
                                    [&](auto &&e) { r = r && e.has_value(); });
        return r;
    }

    template < class Executor, class... Events >
    auto async_join_impl< Executor, Events... >::cancel(error_code ec) -> void
    {
        auto lock = std::unique_lock(mutex_);
        switch (state_)
        {
        case not_waiting:
            assert(not handler_.has_value());
            error_ = ec;
            state_ = error;
            break;

        case waiting:
            assert(handler_.has_value());
            error_ = ec;
            state_ = error;
            [this] {
                auto h = std::move(handler_);
                h.post_completion(error_);
            }();
            break;

        case error:
            break;

        case complete:
            error_ = ec;
            state_ = error;
            break;
        }
    }

    template < class Executor, class... Events >
    template < class Event >
    auto async_join_impl< Executor, Events... >::set_event(Event e) -> void
    {
        auto  lock = std::unique_lock(mutex_);
        auto &opt  = get< std::optional< Event > >(events_);
        assert(not opt.has_value());
        opt = std::move(e);
        if (state_ == waiting and triggered())
        {
            state_ = complete;
            auto h = std::move(handler_);
            h.post_completion(error_code());
        }
    }

    template < class Executor, class... Events >
    template < class Event >
    auto async_join_impl< Executor, Events... >::unset_event() -> void
    {
        auto  lock = std::unique_lock(mutex_);
        auto &opt  = get< std::optional< Event > >(events_);
        assert(opt.has_value());
        opt.reset();
    }

    //
    // async_event
    //

    template < class Executor >
    template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code))
                   CompletionHandler >
    auto async_event< Executor >::async_wait(CompletionHandler &&token)
        -> BOOST_ASIO_INITFN_RESULT_TYPE(CompletionHandler, void(error_code))
    {
        auto initiate = [this](auto &&handler) {
            assert(not handler_.has_value());

            auto exec = net::get_associated_executor(handler, get_executor());

            auto l = std::unique_lock(mutex_);
            if (handler_.has_value())
            {
                assert(!"logic error");
                l.unlock();
                net::post(exec, [handler = std::move(handler)]() mutable {
                    handler(net::error::operation_not_supported);
                });
            }
            else if (error_)
            {
                auto err = error_;
                l.unlock();
                net::post(exec, [handler = std::move(handler), err]() mutable {
                    handler(err);
                });
            }
            else if (event_set_)
            {
                l.unlock();
                net::post(exec, [handler = std::move(handler)]() mutable {
                    handler(error_code());
                });
            }
            else
            {
                if (exec == get_executor())
                    if constexpr (detail::has_get_executor_v< decltype(
                                      handler) >)
                        handler_ = construct_wait_op(
                            std::move(handler), make_cheap_work_guard(exec));
                    else
                        handler_ = construct_wait_op(
                            net::bind_executor(exec, std::move(handler)),
                            make_cheap_work_guard(exec));
                else
                    handler_ = construct_wait_op(
                        std::move(handler),
                        make_cheap_work_guard(exec),
                        make_cheap_work_guard(get_executor()));
            }
        };

        return net::async_initiate< CompletionHandler, void(error_code) >(
            initiate, token);
    }

}   // namespace notstd::util::async