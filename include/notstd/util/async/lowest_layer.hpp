#pragma once

namespace notstd::util::async
{
    template < class Stream, class = void >
    struct has_next_layer : std::false_type
    {
    };

    template < class Stream >
    struct has_next_layer<
        Stream,
        std::void_t< decltype(std::declval< Stream & >().next_layer()) > >
    : std::true_type
    {
    };

    template < class Stream >
    constexpr bool has_next_layer_v =
        has_next_layer< std::decay_t< Stream > >::value;

    template < class Stream >
    auto get_lowest_layer(Stream &s) -> decltype(auto)
    {
        if constexpr (has_next_layer_v< Stream >)
            return get_lowest_layer(s.next_layer());
        else
            return s;
    }
}   // namespace notstd::util::async