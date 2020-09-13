#pragma once
#include <notstd/util/explain.hpp>
#include <spdlog/spdlog.h>

namespace notstd::util::async
{
    template < class Self, class ContextString = std::string_view >
    struct spawn_handler
    {
        spawn_handler(Self self, ContextString context_string)
        : self_(self)
        , context_string_(std::move(context_string))
        {
        }

        void operator()(std::exception_ptr ep) const
        {
            try
            {
                if (ep)
                    std::rethrow_exception(ep);
            }
            catch(config::system_error& se)
            {
                if (se.code() != config::net::error::operation_aborted)
                    spdlog::error("[{}] exception: {}", context_string_, explain());
            }
            catch(...)
            {
                spdlog::error("[{}] exception: {}", context_string_, explain());
            }
        }

      private:
        Self          self_;
        ContextString context_string_;
    };
}   // namespace notstd::util::async