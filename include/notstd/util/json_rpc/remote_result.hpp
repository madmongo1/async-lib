#pragma once
#include "error.hpp"

#include <boost/variant2/variant.hpp>
#include <notstd/util/error.hpp>
#include <notstd/util/json.hpp>
#include <notstd/util/json_rpc/remote_failure.hpp>

namespace notstd::util::json_rpc
{
    struct no_remote_result : std::logic_error
    {
        using std::logic_error::logic_error;
    };

    /// A type which represents the result of a an RPC call.
    ///
    /// This object can hold either a "result" or an "error" (an exception of
    /// type remote_failure)
    struct remote_result
    {
        using variant_type =
            boost::variant2::variant< error_code, json::value, remote_failure >;

        explicit remote_result(error_code fail = error::empty_result);
        explicit remote_result(remote_failure fail);
        explicit remote_result(boost::json::value result);

        /// Returns true if the request failed at the remote end
        bool is_remote_failure() const;
        bool is_error() const;
        bool is_result() const;

        auto assign(json::value v) -> json::value &
        {
            return impl_.emplace< json::value >(std::move(v));
        }

        auto assign(remote_failure f) -> remote_failure &
        {
            return impl_.emplace< remote_failure >(std::move(f));
        }

        /// Will throw if `is_error()`
        /// @return reference to the returned value
        json::value const &get() const;
        json::value &      get();
        json::value const &operator*() const { return get(); };

        /// Will throw if not remote_error
        remote_failure & get_remote_failure();

        auto as_variant() -> variant_type & { return impl_; }
        auto as_variant() const -> variant_type const & { return impl_; }

      private:
        variant_type impl_;
    };

    auto operator<<(std::ostream &os, remote_result const &arg)
        -> std::ostream &;
}   // namespace notstd::util::json_rpc