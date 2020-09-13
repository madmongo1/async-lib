#include <fmt/ostream.h>
#include <notstd/util/net.hpp>

namespace notstd::util
{
    auto operator<<(std::ostream &os, print_wrapper< error_code > const &wrap)
        -> std::ostream &
    {
        fmt::print(os,
                   "[error_code [message {}] [value {}] [cat {}]",
                   wrap.arg.message(),
                   wrap.arg.value(),
                   wrap.arg.category().name());
        return os;
    }

}   // namespace notstd::util