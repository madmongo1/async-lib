#include <catch2/catch.hpp>
#include <notstd/util/async/websocket_connect_state_impl.hpp>

using namespace notstd::util;

TEST_CASE("notstd::util::async::websocket_connect_state_impl")
{
    auto ioc         = net::io_context();
    auto ssl_context = net::ssl::context(net::ssl::context_base::tls_client);
    ssl_context.set_default_verify_paths();
    ssl_context.set_verify_mode(net::ssl::verify_peer);

    using executor_type = net::io_context::executor_type;

    using layer_0 = net::basic_stream_socket< net::ip::tcp, executor_type >;
    using layer_1 = net::ssl::stream< layer_0 >;
    using websock_type = websocket::stream< layer_1 >;
    auto websock       = websock_type(ioc.get_executor(), ssl_context);
    auto state         = async::make_connect_state_impl(websock);
    using state_type   = decltype(state);

    std::exception_ptr run_exception = nullptr;
    bool               run_completed = false;

    auto spawn_run = [&] {
        run_exception = nullptr;
        run_completed = false;
        net::co_spawn(
            ioc.get_executor(),
            [&]() -> state_type::awaitable {
                auto my_exec = co_await net::this_coro::executor;
                REQUIRE(my_exec == websock.get_executor());

                co_return co_await state(
                    "test.deribit.com", "https", "/ws/api/v2/");
            },
            [&](std::exception_ptr ep) {
                run_exception = ep;
                run_completed = true;
            });
    };

    spawn_run();

    SECTION("immediate cancel")
    {
        net::post(ioc.get_executor(), [&] { state.cancel(); });
        ioc.run();
        CHECK(run_exception);
        CHECK(run_completed);
    }

    SECTION("run to completion")
    {
        ioc.run();
        CHECK(not run_exception);
        CHECK(run_completed);
    }
}