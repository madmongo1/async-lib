#pragma once
#include <ostream>

namespace notstd::util
{
    template < class T, typename Enable = void >
    struct print_wrapper;

    template < class Arg >
    auto print(Arg const &arg)
    {
        return print_wrapper< std::decay_t< Arg > > { arg };
    }

    template < class T, class = void >
    struct is_ostreamable : std::false_type
    {
    };
    template < class T >
    struct is_ostreamable<
        T,
        std::void_t< decltype(std::declval< std::ostream & >()
                              << std::declval< const T & >()) > >
    : std::true_type
    {
    };

    template < class T >
    constexpr bool is_ostreamable_v = is_ostreamable< T >::value;

    template < class T >
    struct print_wrapper< T, std::enable_if_t< is_ostreamable_v< T > > >
    {
        T const &arg;

        friend auto operator<<(std::ostream &os, print_wrapper const &wrapper)
            -> std::ostream &
        {
            return os << wrapper.arg;
        }
    };

    template < class T, class = void >
    struct is_container_of_printables : std::false_type
    {
    };
    template < class T >
    struct is_container_of_printables< T,
                                       std::void_t< decltype(print(*std::begin(
                                           std::declval< const T & >()))) > >
    : std::true_type
    {
    };

    template < class T >
    constexpr auto is_container_of_printables_v =
        is_container_of_printables< T >::value;

    /// Looks like a container of printables
    /// @tparam T
    template < class T >
    struct print_wrapper<
        T,
        std::enable_if_t< not is_ostreamable_v< T > and
                          is_container_of_printables_v< T > > >
    {
        T const &arg;

        friend auto operator<<(std::ostream &os, print_wrapper const &wrapper)
            -> std::ostream &
        {
            auto sep = "";
            os << '(';
            for (auto &&elem : wrapper.arg)
            {
                os << sep << print(elem);
                sep = ", ";
            }
            return os << ')';
        }
    };

}   // namespace notstd::util