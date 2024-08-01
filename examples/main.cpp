#include <iostream>

#include <coroutine>
#include <stdexec/execution.hpp>
#include <exec/task.hpp>

namespace x = stdexec;

template <x::sender S1, x::sender S2>
exec::task<int> async_answer(S1 s1, S2 s2) {
    // Senders are implicitly awaitable (in this coroutine type):
    co_await static_cast<S2&&>(s2);
    co_return co_await static_cast<S1&&>(s1);
}

template <x::sender S1, x::sender S2>
exec::task<std::optional<int>> async_answer2(S1 s1, S2 s2) {
    co_return co_await x::stopped_as_optional(async_answer(s1, s2));
}

// tasks have an associated stop token
exec::task<std::optional<x::inplace_stop_token>> async_stop_token() { // NOLINT(*-static-accessed-through-instance)
    co_return co_await x::stopped_as_optional(x::get_stop_token());
}

int main() {
    try {
        // Awaitables are implicitly senders:
        auto [i] = x::sync_wait(async_answer2(x::just(42), x::just(3))).value();
        std::cout << "The answer is " << i.value() << '\n';
    } catch (std::exception& e) {
        std::cout << e.what() << '\n';
    }
}