#pragma once

#include <cstdint>
#include <notstd/util/async/poly_handler.hpp>
#include <notstd/util/json_rpc/remote_result.hpp>
#include <unordered_map>

namespace notstd::util::json_rpc
{
    struct request_map
    {
      private:
        using handler_type =
            async::poly_handler< void(error_code, remote_result) >;

        struct request_event
        {
            std::string  method;
            json::value  params;
            handler_type handler;
        };

        using id_type = std::uint64_t;
        using outstanding_request_map =
            std::unordered_map< id_type, handler_type >;
        using pending_auth_container = std::vector< request_event >;

      public:

        request_map();

        /// Create an RPC request frame from the given method and parameters.
        /// Associate the given completion handler with the generated request id
        /// and store for later completion. Return the frame so that it can be
        /// scheduled for sending on some transport
        /// @note This function uses completion handlers, not completion tokens.
        template < class Continuation,
                   BOOST_ASIO_COMPLETION_HANDLER_FOR(
                       void(error_code, remote_result)) CompletionHandler >
        auto add_async_request(std::string_view    method,
                               boost::json::value  params,
                               Continuation &&     cont,
                               CompletionHandler &&handler) -> bool
        {
            if (auth_state_ != authenticated and method.starts_with("private/"))
            {
                pending_authentication_.emplace_back(request_event {
                    .method  = std::string(method.begin(), method.end()),
                    .params  = std::move(params),
                    .handler = handler_type(
                        std::forward< CompletionHandler >(handler)) });
                return false;
            }
            else
            {
                auto id = ++current_id_;
                auto frame =
                    boost::json::value({ { "id", id },
                                         { "method", json::convert(method) },
                                         { "jsonrpc", "2.0" },
                                         { "params", std::move(params) } });
                outstanding_.emplace(
                    id,
                    handler_type(std::forward< CompletionHandler >(handler)));
                cont(std::move(frame));
                return true;
            }
        }

        /// Find the associated handler for the JSON RPC response and complete
        /// it. If the JSON is malformed or the outstanding request does not
        /// exist, throw an exeception
        /// @param response_value is a JSON RPC response
        auto async_complete(json::value jframe) -> void;

        template < class Cont >
        auto notify_autenticated(Cont &&cont)
        {
            assert(auth_state_ == not_authenticated);
            auth_state_ = authenticated;
            auto cpy    = std::move(pending_authentication_);
            for (auto &req : cpy)
            {
                auto id = ++current_id_;
                auto frame =
                    boost::json::value({ { "id", id },
                                         { "method", req.method },
                                         { "jsonrpc", "2.0" },
                                         { "params", std::move(req.params) } });
                outstanding_.emplace(id, std::move(req.handler));
                cont(std::move(frame));
            }
        }

        auto cancel(error_code ec = net::error::operation_aborted) -> void;

      private:
        enum auth_state
        {
            not_authenticated,
            authenticated
        };

      private:
        /// Requests currently in flight
        std::unordered_map< int, handler_type > outstanding_;

        /// Requests pending authentication
        std::vector< request_event > pending_authentication_;
        std::int64_t                 current_id_;

        auth_state auth_state_;
    };
}   // namespace notstd::util::json_rpc