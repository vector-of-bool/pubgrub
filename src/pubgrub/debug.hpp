#pragma once

#include "./concepts.hpp"

#include <neo/repr.hpp>
#include <neo/ufmt.hpp>

#include <string_view>

namespace pubgrub::debug {

template <typename T>
struct try_repr {
    const T& item;

    friend void do_repr(auto out, const try_repr* self) {
        if constexpr (decltype(out)::template can_repr<T>) {
            if constexpr (out.just_type) {
                out.type("{}", out.template repr_type<T>());
            } else if constexpr (out.just_value) {
                out.value("{}", out.repr_value(self->item));
            } else {
                out.append("{}", out.repr_sub(self->item));
            }
        } else {
            out.append("[unrepresentable]");
        }
    }

    std::string to_string() const noexcept { return neo::repr(*this).string(); }
};

template <typename T>
concept debugger = requires(T& dbg, std::string_view strv) {
    {dbg.debug(strv)};
};

template <typename D, neo::formattable... Args>
void debug(D&&              dbg,
           std::string_view fmt [[maybe_unused]],
           const Args&... args [[maybe_unused]]) noexcept {
    if constexpr (debugger<D>) {
        const std::string s  = neo::ufmt(fmt, args...);
        std::string_view  sv = s;
        dbg.debug(sv);
    }
}

}  // namespace pubgrub::debug
