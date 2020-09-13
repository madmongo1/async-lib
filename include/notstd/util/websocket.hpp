#pragma once

#include <notstd/config/websocket.hpp>
#include <notstd/util/beast.hpp>
#include <notstd/util/net.hpp>
#include <fmt/ostream.h>

namespace notstd::util
{
    namespace websocket = notstd::config::websocket;

    template<class NextLayer>
    struct print_wrapper<websocket::stream<NextLayer>>
    {
        websocket::stream<NextLayer> const& arg;

        print_wrapper(websocket::stream<NextLayer> const& arg) : arg(arg) {}

        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&
        {
            os << print(wrap.arg.next_layer());
            return os;
        }
    };

    template<>
    struct print_wrapper<websocket::close_reason>
    {
        websocket::close_reason const& arg;

        print_wrapper(websocket::close_reason const& arg) : arg(arg) {}

        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&
        {
            fmt::print(os, "[ws close [code {}] [reason {}]]", wrap.arg.code, wrap.arg.reason);
            return os;
        }
    };

}
