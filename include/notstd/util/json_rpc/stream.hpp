#pragma once
#include <notstd/util/net.hpp>
#include <notstd/util/websocket.hpp>

namespace notstd::util::json_rpc
{

    template<class NextLayer>
    struct stream_impl
    {
        using websock = websocket::stream<NextLayer>;

        using executor_type = Executor;


    };

    struct stream
    {

    };
}