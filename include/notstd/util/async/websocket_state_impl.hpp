#pragma once
#include <fmt/ostream.h>
#include <notstd/util/async/async_join_impl.hpp>
#include <notstd/util/async/executor_traits.hpp>
#include <notstd/util/async/queue_impl.hpp>
#include <notstd/util/websocket.hpp>
#include <span>
#include <spdlog/spdlog.h>

namespace notstd::util::async
{
    template < class NextLayer, class TextType = std::string >
    struct websocket_state_impl
    : executor_traits< typename NextLayer::executor_type >
    {
        using traits_type =
            executor_traits< typename NextLayer::executor_type >;
        using stream_type   = websocket::stream< NextLayer >;
        using executor_type = typename traits_type::executor_type;
        using awaitable     = typename traits_type::template awaitable< void >;

        /// Construct with arguments necessary to construct the websocket stream
        /// @tparam Args
        /// @param args
        template < class... Args >
        websocket_state_impl(Args &&... args);

        auto get_executor() -> executor_type { return stream_.get_executor(); }

        /// Model of a frame handler which ignores incoming data
        struct null_frame_handler
        {
            void operator()(std::span< char >) const {}
        };

        /// Run the state, calling the supplied frame handlers as frames arrive
        /// @tparam OnTextFrame
        /// @tparam OnBinaryFrame
        /// @param on_text The handler to call when a text frame arrives. This
        /// shall be called in the context of this state's executor. Note that
        /// this handler is destroyed prior to the end of this coroutine. It is
        /// therefore reasonable to for the handler to own lifetimes of
        /// dependent objects if necessary.
        /// @param on_binary The handler to call when a binary frame arrives.
        /// This argument defaults to a no-op as most websockets are text. This
        ///  shall be called in the context of this state's executor. Note that
        ///  this handler is destroyed prior to the end of this coroutine. It is
        ///  therefore reasonable to for the handler to own lifetimes of
        ///  dependent objects if necessary.
        /// @return
        template < class OnTextFrame, class OnBinaryFrame = null_frame_handler >
        auto operator()(OnTextFrame &&  on_text,
                        OnBinaryFrame &&on_binary = OnBinaryFrame())
            -> awaitable;

        /// Notify the websocket that it should close. Successful closing of the
        /// websocket is indicated by the return of the operator() coroutine
        /// @param reason
        auto close(websocket::close_reason reason =
                       websocket::close_code::going_away) -> void;

        /// Notify the websocket that it should send a text frame as soon as
        /// possible. No indication is fed back to the caller as to whether the
        /// frame was ever actually sent.
        /// @pre websocket is connected
        /// @exception system_error is thrown if the websocket is not connected
        /// at the time of this call
        /// @param text
        auto send_text(TextType text) -> void;

        using connect_awaitable = net::awaitable< void, executor_type >;

        auto connect(std::string host, std::string port, std::string target)
            -> connect_awaitable;

      private:
        /// The substate that controls writing of frames
        struct write_state_impl
        {
            write_state_impl(websocket_state_impl &outer_state);
            auto operator()() -> net::awaitable< void, executor_type >;
            auto cancel(error_code ec = net::error::operation_aborted) -> void;
            friend auto operator<<(std::ostream &          os,
                                   write_state_impl const &state)
                -> std::ostream &
            {
                fmt::print(os, "[{}::write]", print(state.outer_state_));
                return os;
            }

          private:
            websocket_state_impl &               outer_state_;
            std::function< void(error_code ec) > on_cancel_ = nullptr;
        };

        /// The substate that controls reading and error handling
        struct read_state_impl
        {
            read_state_impl(websocket_state_impl &outer_state);
            auto        operator()() -> net::awaitable< void, executor_type >;
            friend auto operator<<(std::ostream &         os,
                                   read_state_impl const &state)
                -> std::ostream &
            {
                fmt::print(os, "[{}::read]", print(state.outer_state_));
                return os;
            }

          private:
            websocket_state_impl &outer_state_;
        };

        /// The substate that controls orderly shutdown
        struct close_state_impl
        {
            close_state_impl(websocket_state_impl &outer_state);
            auto operator()() -> net::awaitable< void, executor_type >;

            // immediately cancel the close state with error
            auto cancel(error_code ec = net::error::operation_not_supported)
                -> void;

            // initiate clean shutdown
            auto        close(websocket::close_reason reason) -> void;
            friend auto operator<<(std::ostream &          os,
                                   close_state_impl const &state)
                -> std::ostream &
            {
                fmt::print(os, "[{}::close]", print(state.outer_state_));
                return os;
            }

          private:
            websocket_state_impl &outer_state_;
            async_join_impl< executor_type, websocket::close_reason >
                                                           close_latch_;
            std::function< void(websocket::close_reason) > on_close_;
            std::function< void(error_code) >              on_cancel_;
        };

        friend auto operator<<(std::ostream &              os,
                               websocket_state_impl const &state)
            -> std::ostream &
        {
            fmt::print(os, "[websocket {}]", print(state.stream_));
            return os;
        }

        /// Stores a deferred connect request
        struct connect_request
        {
            std::string host;
            std::string port;
            std::string target;
        };

      private:
        stream_type                                       stream_;
        std::function< void(websocket::close_reason) >    on_close_;
        async_join_impl< executor_type, connect_request > connect_latch_;
        async_event< executor_type >                      connected_signal_;
        std::function< void(std::span< char >) > on_text_frame_   = nullptr;
        std::function< void(std::span< char >) > on_binary_frame_ = nullptr;
        std::function< void(TextType) >          on_send_text_    = nullptr;
    };
}   // namespace notstd::util::async

#include <notstd/util/async/websocket_connect_state_impl.hpp>

namespace notstd::util::async
{
    template < class NextLayer, class TextType >
    template < class... Args >
    websocket_state_impl< NextLayer, TextType >::websocket_state_impl(
        Args &&... args)
    : stream_(std::forward< Args >(args)...)
    , connect_latch_(get_executor())
    , connected_signal_(get_executor())
    {
    }

    template < class NextLayer, class TextType >
    template < class OnTextFrame, class OnBinaryFrame >
    auto websocket_state_impl< NextLayer, TextType >::operator()(
        OnTextFrame &&  on_text,
        OnBinaryFrame &&on_binary) -> awaitable
    {
        auto close_request = std::optional< websocket::close_reason >();
        on_text_frame_     = std::forward< OnTextFrame >(on_text);
        on_binary_frame_   = std::forward< OnBinaryFrame >(on_binary);

        try
        {
            //
            // wait for connect request
            //

            on_close_ = [&](websocket::close_reason reason) {
                spdlog::trace("{} close requested: {}", *this, print(reason));
                close_request = reason;
                connect_latch_.cancel();
            };
            co_await connect_latch_.async_wait(
                net::use_awaitable_t< executor_type >());
            if (close_request)
                throw system_error(net::error::connection_aborted);

            //
            // start connection
            //

            auto connect_state = make_connect_state_impl(stream_);
            on_close_          = [&](websocket::close_reason reason) {
                spdlog::trace("{} close requested: {}", *this, print(reason));
                close_request = reason;
                connect_state.cancel();
            };
            auto cr = get< connect_request & >(connect_latch_.events());
            spdlog::trace(
                "{} connect: {}://{}{}", *this, cr.port, cr.host, cr.target);
            co_await connect_state(cr.host, cr.port, cr.target);
            spdlog::trace("{} connection up", *this);

            //
            // fork into read, write and close states
            //

            struct writer_done
            {
            };
            struct closer_done
            {
            };

            auto write_state = write_state_impl(*this);
            auto close_state = close_state_impl(*this);
            auto read_state  = read_state_impl(*this);

            // provide a rendezvous object so that the orthognoal regions of
            // read, write and close can join. The read state shall be the
            // parent 'thread' so no need to provide an event for it, as it will
            // initiate the join
            auto connected_join =
                async_join_impl< executor_type, writer_done, closer_done >(
                    get_executor());

            // fork the write state and ensure that it notifies the rendezvous
            // when it exits (for any reason)
            net::co_spawn(
                get_executor(),
                [&]() -> net::awaitable< void, executor_type > {
                    co_await write_state();
                },
                [&](std::exception_ptr ep) {
                    spdlog::trace("{} write exit: {}", *this, explain(ep));
                    connected_join.set_event(writer_done());
                });

            // fork the close state and ensure that it notifies the rendezvous
            // when it exits (for any reason)
            net::co_spawn(
                get_executor(),
                [&]() -> net::awaitable< void, executor_type > {
                    co_await close_state();
                },
                [&](std::exception_ptr ep) {
                    spdlog::trace("{} close exit: {}", *this, explain(ep));
                    connected_join.set_event(closer_done());
                });

            // run the read state until complete
            try
            {
                // we set the "connected" event here to ensure that the write
                // and close states have been initiated before a client is
                // allowed to send data
                connected_signal_.set_event();

                // the action to take upon receipt of a close request
                auto action_close = [&](websocket::close_reason reason) {
                    spdlog::trace("{} close requested: {}", *this, print(reason));
                    close_state.close(reason);
                    write_state.cancel();
                };

                // if the close request has already arrived, action it now,
                // otherwise set up the event handler action it when it arrives
                if (close_request)
                    action_close(*close_request);
                else
                    on_close_ = [&](websocket::close_reason reason) {
                        action_close(reason);
                    };

                // run the read state to completion - this means either a comms
                // error or a succesful close
                co_await read_state();
            }
            catch (...)
            {
                spdlog::trace("{} read exception: {}", *this, explain());
                // if we get a read error, we must ensure that the write and
                // close states are properly canceled, otherwise the join may
                // not happen
                write_state.cancel();
                close_state.cancel();
            }

            // join the forked coroutines
            connected_join.async_wait(net::use_awaitable_t< executor_type >());

            on_text_frame_   = nullptr;
            on_binary_frame_ = nullptr;

            co_return;
        }
        catch (system_error &se)
        {
            spdlog::trace("{} exception: {}", *this, explain());
            connected_signal_.cancel(se.code());
            on_close_        = nullptr;
            on_text_frame_   = nullptr;
            on_binary_frame_ = nullptr;
            if (not close_request)
                throw;
        }
        catch (...)
        {
            spdlog::trace("{} exception: {}", *this, explain());
            connected_signal_.cancel(net::error::fault);
            on_close_        = nullptr;
            on_text_frame_   = nullptr;
            on_binary_frame_ = nullptr;
            if (not close_request)
                throw;
        }
    }

    template < class NextLayer, class TextType >
    auto websocket_state_impl< NextLayer, TextType >::close(
        websocket::close_reason reason) -> void
    {
        assert(net::is_correct_thread(get_executor()));
        if (on_close_)
            on_close_(reason);
    }

    template < class NextLayer, class TextType >
    auto
    websocket_state_impl< NextLayer, TextType >::connect(std::string host,
                                                         std::string port,
                                                         std::string target)
        -> connect_awaitable
    try
    {
        assert(co_await net::this_coro::executor == get_executor());
        assert(not connect_latch_.triggered());

        spdlog::trace(
            "{}::connect({}, {}, {}) starting", *this, host, port, target);

        connect_latch_.set_event(
            connect_request { .host   = std::move(host),
                              .port   = std::move(port),
                              .target = std::move(target) });

        co_await connected_signal_.async_wait(
            net::use_awaitable_t< executor_type >());

        spdlog::trace("{}::connect complete", *this);

        co_return;
    }
    catch (...)
    {
        spdlog::trace("{}::connect exception: {}", *this, explain());
        throw;
    }

    //
    // write state
    //

    template < class NextLayer, class TextType >
    websocket_state_impl< NextLayer, TextType >::write_state_impl::
        write_state_impl(websocket_state_impl &outer_state)
    : outer_state_(outer_state)
    {
    }

    template < class NextLayer, class TextType >
    auto
    websocket_state_impl< NextLayer, TextType >::write_state_impl::operator()()
        -> net::awaitable< void, executor_type >
    try
    {
        auto my_error = error_code();
        auto tx_queue =
            queue_impl< TextType, executor_type >(outer_state_.get_executor());

        outer_state_.on_send_text_ = [&](TextType text) {
            tx_queue.push(std::move(text));
        };

        while (!my_error)
        {
            on_cancel_ = [&](error_code ec) {
                my_error = ec;
                tx_queue.cancel(ec);
                spdlog::trace("{}::on_cancel({})", *this, print(ec));
                outer_state_.on_send_text_ = nullptr;
            };
            auto frame =
                co_await tx_queue.async_pop(outer_state_.use_awaitable);
            if (my_error)
                break;

            on_cancel_ = nullptr;
            co_await outer_state_.stream_.async_write(
                net::buffer(frame.data(), frame.size()),
                outer_state_.use_awaitable);
        }

        outer_state_.on_send_text_ = nullptr;

        co_return;
    }
    catch (...)
    {
        outer_state_.on_send_text_ = nullptr;
        spdlog::trace("{} exception: {}", *this, explain());
        throw;
    }

    template < class NextLayer, class TextType >
    auto websocket_state_impl< NextLayer, TextType >::write_state_impl::cancel(
        error_code ec) -> void
    {
        on_cancel_(ec);
    }

    //
    // close state
    //

    template < class NextLayer, class TextType >
    websocket_state_impl< NextLayer, TextType >::close_state_impl::
        close_state_impl(websocket_state_impl &outer_state)
    : outer_state_(outer_state)
    , close_latch_(outer_state_.get_executor())
    {
    }

    template < class NextLayer, class TextType >
    auto
    websocket_state_impl< NextLayer, TextType >::close_state_impl::operator()()
        -> net::awaitable< void, executor_type >
    try
    {
        on_close_ = [&](websocket::close_reason reason) {
            close_latch_.set_event(reason);
            spdlog::trace("{}::on_close({})", *this, print(reason));
        };

        on_cancel_ = [&](error_code ec) {
            spdlog::trace("{}::on_cancel({})", *this, print(ec));
            close_latch_.cancel(ec);
        };

        spdlog::trace("{} waiting latch", *this);
        co_await close_latch_.async_wait(outer_state_.use_awaitable);
        on_close_  = nullptr;
        on_cancel_ = nullptr;

        auto &reason = get< websocket::close_reason & >(close_latch_.events());
        co_await outer_state_.stream_.async_close(reason,
                                                  outer_state_.use_awaitable);

        co_return;
    }
    catch (...)
    {
        on_close_  = nullptr;
        on_cancel_ = nullptr;
        spdlog::trace("{} exception: {}", *this, explain());
        throw;
    }

    template < class NextLayer, class TextType >
    auto websocket_state_impl< NextLayer, TextType >::close_state_impl::close(
        websocket::close_reason reason) -> void
    {
        if (on_close_)
            on_close_(reason);
    }

    template < class NextLayer, class TextType >
    auto websocket_state_impl< NextLayer, TextType >::close_state_impl::cancel(
        error_code ec) -> void
    {
        if (on_cancel_)
            on_cancel_(ec);
    }

    //
    // read state
    //

    template < class NextLayer, class TextType >
    websocket_state_impl< NextLayer, TextType >::read_state_impl::
        read_state_impl(websocket_state_impl &outer_state)
    : outer_state_(outer_state)
    {
    }

    template < class NextLayer, class TextType >
    auto
    websocket_state_impl< NextLayer, TextType >::read_state_impl::operator()()
        -> net::awaitable< void, executor_type >
    {
        beast::flat_buffer rxbuf;
        try
        {
            for (;;)
            {
                auto bytes = co_await outer_state_.stream_.async_read(
                    rxbuf, outer_state_.use_awaitable);
                auto buf   = rxbuf.data();
                auto first = reinterpret_cast< char * >(buf.data());
                if (outer_state_.stream_.got_text())
                {
                    if (outer_state_.on_text_frame_)
                        outer_state_.on_text_frame_(
                            std::span< char >(first, bytes));
                }
                else if (outer_state_.stream_.got_binary())
                {
                    if (outer_state_.on_binary_frame_)
                        outer_state_.on_binary_frame_(
                            std::span< char >(first, bytes));
                }
                else
                {
                    // ignore whatever this is
                }
                rxbuf.consume(bytes);
            }
        }
        catch (system_error &se)
        {
            if (se.code() != websocket::error::closed)
                throw;
        }
    }

    template < class NextLayer, class TextType >
    auto websocket_state_impl< NextLayer, TextType >::send_text(TextType text)
        -> void
    {
        if (!on_send_text_)
            throw system_error(net::error::not_connected);
        on_send_text_(std::move(text));
    }

}   // namespace notstd::util::async