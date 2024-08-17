/*
 * Copyright (c) 2022 Lucian Radu Teodorescu
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * General context:
 * - server application that processes images
 * - execution contexts:
 *    - 1 dedicated thread for network I/O
 *    - N worker threads used for CPU-intensive work
 *    - M threads for auxiliary I/O
 *    - optional GPU context that may be used on some types of servers
 *
 * Specific problem description:
 * - reading data from the socket before processing the request
 * - reading of the data is done on the I/O context
 * - no processing of the data needs to be done on the I/O context
 *
 * Example goals:
 * - show how one can change the execution context
 * - exemplify the use of `on` and `transfer` algorithms
 */

#include <iostream>
#include <array>
#include <string_view>
#include <cstring>
#include <mutex>

// Pull in the reference implementation of P2300:
#include <range/v3/all.hpp>
#include <spdlog/spdlog.h>
#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>
// Use a thread pool
#include "exec/static_thread_pool.hpp"



#include <iomanip>
#include <iostream>
#include <string>

namespace vz {
    struct Counters {
        unsigned m_def_ctor    = 0;
        unsigned m_copy_ctor   = 0;
        unsigned m_move_ctor   = 0;
        unsigned m_copy_assign = 0;
        unsigned m_move_assign = 0;
        unsigned m_dtor        = 0;

        void reset() {
            *this = {};
        }

        bool leaks() const {
            return m_def_ctor + m_copy_ctor + m_move_ctor != m_dtor;
        }

        auto numbers() const {
            return std::format(
                "Default constructor count: {:2}\nCopy constructor count:    {:2}\nMove constructor count:    {:2}\nCopy assignment count:     {:2}\nMove assignment count:     {:2}\nDestructor count:          {:2}",
                 m_def_ctor   ,
                 m_copy_ctor  ,
                 m_move_ctor  ,
                 m_copy_assign,
                 m_move_assign,
                 m_dtor
            );
        }

        friend bool operator==(const Counters& lhs, const Counters& rhs) {
            return
                lhs.m_def_ctor    == rhs.m_def_ctor    &&
                lhs.m_copy_ctor   == rhs.m_copy_ctor   &&
                lhs.m_move_ctor   == rhs.m_move_ctor   &&
                lhs.m_copy_assign == rhs.m_copy_assign &&
                lhs.m_move_assign == rhs.m_move_assign &&
                lhs.m_dtor        == rhs.m_dtor        ;
        }

        friend bool operator!=(const Counters& lhs, const Counters& rhs) { return !(lhs == rhs); }

    };
}

template <> struct fmt::formatter<vz::Counters>: formatter<std::string> {
    // parse is inherited from formatter<string_view>.
    auto format(vz::Counters const& c, format_context& ctx) const -> format_context::iterator {
        return formatter<std::string>::format(c.numbers(), ctx);
    }
};

namespace vz::detail {

struct Globals {
    ~Globals() {
        if (m_verbose)
           spdlog::info("\n===== Noisy counters =====\n{}", m_counters);
    }

    Counters m_counters;
    unsigned m_next_id = 0;
    bool     m_verbose = true;
};

}

namespace  vz {
class Noisy {
private:
    static detail::Globals& globals() {
        static detail::Globals s_globals;
        return s_globals;
    }

public:
    static Counters& counters() { return globals().m_counters; }
    static void set_verbose(bool verbose) { globals().m_verbose = verbose; }

    Noisy() {
        if (globals().m_verbose)
            spdlog::info("{}: default constructor ", this->id());
        globals().m_counters.m_def_ctor++;
    }

    ~Noisy() {
        if (globals().m_verbose)
            spdlog::info("{}: destructor ", this->id());
        globals().m_counters.m_dtor++;
    }

    Noisy(const Noisy& other) {
        if (globals().m_verbose)
            spdlog::info("{}: copy constructor from {} ", this->id(), other.id());
        globals().m_counters.m_copy_ctor++;
    }

    Noisy(Noisy&& other) noexcept {
        if (globals().m_verbose)
            spdlog::info("{}: move constructor from {} ", this->id(), other.id());
        globals().m_counters.m_move_ctor++;
    }


    Noisy& operator=(const Noisy& other) {
        if (globals().m_verbose)
            spdlog::info("{}: copy assignment from {} ", this->id(), other.id());
        globals().m_counters.m_copy_assign++;
        return *this;
    }

    Noisy& operator=(Noisy&& other) noexcept {
        if (globals().m_verbose)
            spdlog::info("{}: move assignment from {} ", this->id(), other.id());
        globals().m_counters.m_move_assign++;
        return *this;
    }

    [[nodiscard]] unsigned id() const { return m_id; }

private:
    unsigned m_id = globals().m_next_id++;
};

}


namespace ex = stdexec;

auto ingest(std::unique_ptr<vz::Noisy> buffer) {
    return buffer;
}

auto process_read_data(auto read_data) {
    spdlog::info("Processing Noisy({})", read_data->id());
    return read_data;
}

int main() {
    // Create a thread pool and get a scheduler from it
    exec::static_thread_pool work_pool{8};
    ex::scheduler auto work_sched = work_pool.get_scheduler();

    exec::static_thread_pool io_pool{1};
    ex::scheduler auto io_sched = io_pool.get_scheduler();
    vz::Noisy::set_verbose(false);
    try {
        exec::async_scope scope;
        // Fake a couple of requests
        for (int i = 0; i < 1; i++) {

            // The entire flow
            auto snd =
                // start by reading data on the I/O thread
                ex::on(io_sched, ex::just(std::make_unique<vz::Noisy>()) | ex::then(ingest))
                // do the processing on the worker threads pool
                | ex::transfer(work_sched)
                // process the incoming data (on worker threads)
                | ex::then([](auto&& buffers) {
                    using type = std::remove_cvref_t<decltype(buffers)>;
                    process_read_data(std::forward<type>(buffers));
                })
                // done
                ;

            // execute the whole flow asynchronously
            scope.spawn(std::move(snd));
        }
        (void) stdexec::sync_wait(scope.on_empty());
        spdlog::info("\n===== list =====\n{}",vz::Noisy::counters());
        return 0;

    }
    catch (std::exception const& e) {
        spdlog::error("{}",e.what());
    }
    return 0;
}
