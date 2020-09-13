#include <catch2/catch.hpp>
#include <notstd/util/async/tcp_resolve_state_impl.hpp>

using namespace notstd::util;

TEST_CASE("notstd::util::async::tcp_resolve_state_impl")
{
    auto ioc = net::io_context();
    using executor_type = net::io_context::executor_type;

    using state_type = async::tcp_resolve_state_impl<executor_type>;

    auto state = state_type(ioc.get_executor());

    std::exception_ptr       run_exception = nullptr;
    state_type::results_type run_results;

    auto spawn_run = [&] {
        run_exception = nullptr;
        run_results   = state_type::results_type();
        net::co_spawn(
            ioc.get_executor(),
            [&]() -> net::awaitable< state_type::results_type,
                                     net::io_context::executor_type > {
                auto my_exec = co_await net::this_coro::executor;
                REQUIRE(my_exec == state.get_executor());

                co_return co_await state("test.deribit.com", "https");
            },
            [&](std::exception_ptr ep, state_type::results_type res) {
                run_exception = ep;
                run_results   = res;
            });
    };

    spawn_run();

    SECTION("immediate cancel")
    {
        net::post(ioc.get_executor(), [&] { state.cancel(); });
        ioc.run();
        CHECK(run_exception);
        CHECK(run_results.empty());
        SECTION("successful resolve after cancel")
        {
            ioc.restart();
            spawn_run();
            ioc.run();
            CHECK(not run_exception);
            CHECK(not run_results.empty());
        }
    }

    SECTION("run to completion")
    {
        ioc.run();
        CHECK(not run_exception);
        CHECK(not run_results.empty());
    }
}