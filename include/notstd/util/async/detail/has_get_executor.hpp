#pragma once
#include <type_traits>

namespace notstd::util::async::detail
{
    template < class T, class = void >
    struct has_get_executor : std::false_type
    {
    };
    template < class T >
    struct has_get_executor<
        T,
        std::void_t< decltype(std::declval< const T & >().get_executor()) > >
    : std::true_type
    {
    };
    template < class T >
    constexpr inline bool has_get_executor_v =
        has_get_executor< std::decay_t< T > >::value;
}   // namespace notstd::util::async::detail