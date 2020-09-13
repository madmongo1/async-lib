#pragma once

#include <notstd/util/error.hpp>
#include <notstd/config/net.hpp>
#include <notstd/util/print.hpp>
namespace notstd::util::net
{
    using namespace notstd::config::net;
}

namespace notstd::util
{
    template<>
    struct print_wrapper<error_code>
    {
        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&;

        error_code const& arg;
    };

    template<class Protocol>
    struct print_wrapper<net::ip::basic_resolver_entry<Protocol>>
    {
        net::ip::basic_resolver_entry<Protocol> const& arg;
        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&
        {
            return os << wrap.arg.endpoint();
        }
    };

    template<class Executor>
    struct print_wrapper<net::basic_socket<net::ip::tcp, Executor>>
    {
        net::basic_socket<net::ip::tcp, Executor> const& arg;

        print_wrapper(net::basic_socket<net::ip::tcp, Executor> const& arg) : arg(arg) {}

        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&
        {
            auto& sock = wrap.arg;
            auto error = error_code();
            auto local = sock.local_endpoint(error);
            if (error)
                os << "unbound";
            else
            {
                os << local;
                os << "->";
                auto remote = sock.remote_endpoint(error);
                if (error)
                    os << "disconnected";
                else
                    os << remote;
            }
            return os;
        }
    };

    template<class Protocol, class Executor>
    struct print_wrapper<net::basic_stream_socket<Protocol, Executor>>
    {
        net::basic_stream_socket<Protocol, Executor> const& arg;

        print_wrapper(net::basic_stream_socket<Protocol, Executor> const& arg) : arg(arg) {}

        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&
        {
            auto& sock = wrap.arg;

            auto error = error_code();
            auto local = sock.local_endpoint(error);
            if (error)
                os << "unbound";
            else
            {
                os << local;
                os << "->";
                auto remote = sock.remote_endpoint(error);
                if (error)
                    os << "disconnected";
                else
                    os << remote;
            }
            return os;
        }
    };

    template<class NextLayer>
    struct print_wrapper<net::ssl::stream<NextLayer>>
    {
        net::ssl::stream<NextLayer> const& arg;

        print_wrapper(net::ssl::stream<NextLayer> const& arg) : arg(arg) {}

        friend auto operator<<(std::ostream& os, print_wrapper const& wrap) -> std::ostream&
        {
            os << print(wrap.arg.next_layer());
            return os;
        }
    };
}