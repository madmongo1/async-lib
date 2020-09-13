#include <catch2/catch.hpp>
#include <notstd/util/async/websocket_state_impl.hpp>

using namespace notstd::util;

TEST_CASE("notstd::util::async::websocket_state_impl")
{
    auto ioc         = net::io_context();
    auto ssl_context = net::ssl::context(net::ssl::context_base::tls_client);
    ssl_context.set_default_verify_paths();
    ssl_context.set_verify_mode(net::ssl::verify_peer);

    using executor_type = net::io_context::executor_type;

    using layer_0 = net::basic_stream_socket< net::ip::tcp, executor_type >;
    using layer_1 = net::ssl::stream< layer_0 >;
    auto state =
        async::websocket_state_impl< layer_1 >(ioc.get_executor(), ssl_context);
    using state_type = decltype(state);

    std::exception_ptr run_exception = nullptr;
    bool               run_completed = false;

    std::string text;
    std::vector<char> bin;

    auto on_text = [&](std::span<char> txt)
    {
        text.assign(txt.begin(), txt.end());
    };

    auto on_binary = [&](std::span<char> b)
    {
        bin.assign(b.begin(), b.end());
    };

    auto spawn_run = [&] {
        run_exception = nullptr;
        run_completed = false;
        net::co_spawn(
            ioc.get_executor(),
            [&]() -> state_type::run_awaitable {
                auto my_exec = co_await net::this_coro::executor;
                REQUIRE(my_exec == state.get_executor());

                co_return co_await state(on_text, on_binary);
            },
            [&](std::exception_ptr ep) {
                run_exception = ep;
                run_completed = true;
            });
    };

    spawn_run();

    SECTION("immediate cancel")
    {
        net::post(ioc.get_executor(), [&] { state.close(); });
        ioc.run();

        // note that closing the connection will cause the run to exit with no error
        CHECK(not run_exception);
        CHECK(run_completed);
    }
    SECTION("connection to completion")
    {
        auto connect_error = std::exception_ptr();
        auto connect_done  = false;

        net::co_spawn(
            ioc.get_executor(),
            [&]() -> state_type::run_awaitable {
                auto my_exec = co_await net::this_coro::executor;
                REQUIRE(my_exec == state.get_executor());

                co_return co_await state.connect(
                    "test.deribit.com", "https", "/ws/api/v2/");
            },
            [&](std::exception_ptr ep) {
                connect_error = ep;
                connect_done  = true;
            });
        while(not connect_done)
        {
            ioc.run_one();
        }
        CHECK(not connect_error);
        CHECK(connect_done);

        //
        // rest of connection here
        //
    }
    /*
        SECTION("run to completion")
        {
            ioc.run();
            CHECK(not run_exception);
            CHECK(run_completed);
        }
        */
}