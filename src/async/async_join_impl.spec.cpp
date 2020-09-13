#include <catch2/catch.hpp>
#include <notstd/util/async/async_join_impl.hpp>

using namespace notstd::util;

TEST_CASE("notstd::util::async::join_impl")
{
    auto ioc = net::io_context();

    struct event_a
    {
    };
    struct event_b
    {
    };

    auto impl = async::async_join_impl< net::io_context::executor_type,
                                        event_a,
                                        event_b >(ioc.get_executor());

    error_code ec_;

    SECTION("captures work guard")
    {
        impl.async_wait([&](error_code ec) { ec_ = ec; });
        auto spins = ioc.poll();
        CHECK(spins == 0);
        CHECK(not ioc.stopped());
        SECTION("cancel immediately")
        {
            impl.cancel();
            CHECK(not ec_);
            spins = ioc.run();
            CHECK(spins == 1);
            CHECK(ioc.stopped());
            CHECK(ec_ == net::error::operation_aborted);
        }
        SECTION("a first")
        {
            impl.set_event(event_a());
            ioc.restart();
            spins = ioc.poll();
            CHECK(spins == 0);
            CHECK(not ioc.stopped());
            CHECK(not ec_);

            SECTION("then b")
            {
                impl.set_event(event_b());
                spins = ioc.run();
                CHECK(spins == 1);
                CHECK(ioc.stopped());
                CHECK(not ec_);
            }
            SECTION("then cancel")
            {
                impl.cancel();
                spins = ioc.run();
                CHECK(spins == 1);
                CHECK(ioc.stopped());
                CHECK(ec_ == net::error::operation_aborted);
            }
        }
        SECTION("b first")
        {
            impl.set_event(event_b());
            ioc.restart();
            spins = ioc.poll();
            CHECK(spins == 0);
            CHECK(not ioc.stopped());
            CHECK(not ec_);
            SECTION("then a")
            {
                impl.set_event(event_a());
                spins = ioc.run();
                CHECK(spins == 1);
                CHECK(ioc.stopped());
                CHECK(not ec_);
            }

            SECTION("then cancel")
            {
                impl.cancel();
                spins = ioc.run();
                CHECK(spins == 1);
                CHECK(ioc.stopped());
                CHECK(ec_ == net::error::operation_aborted);
            }
        }
    }
}