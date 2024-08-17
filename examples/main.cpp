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
#include <chrono>
#include <cstring>
#include <mutex>

// Pull in the reference implementation of P2300:
#include <range/v3/all.hpp>
#include <spdlog/spdlog.h>
#include <fmt/ranges.h>
#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>
#include <range/v3/all.hpp>
// Use a thread pool
#include "exec/static_thread_pool.hpp"

namespace ex = stdexec;
struct task {
    std::string name_;
    void run(task const&){}
    void error(std::exception_ptr err){  std::rethrow_exception(err); }
    void stopped(){}
    void write_to_file(){}
};

struct task_mainteriner {
    std::vector<task> tasks_;

};

int main() {

    auto polling_work = [finished = false /* zmq */] (task_mainteriner&& mainteriner) mutable -> void
    {
        while (not finished)
        {
            auto next_execution_time = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};

            ranges::for_each(mainteriner.tasks_, [](auto const& task) {
                spdlog::info("name {}", task.name_);
                std::this_thread::sleep_for(std::chrono::microseconds{50});
            });

            // Check if we can start any tasks

            // if we can start a task launch it ("fire the missles" - ltesta)
            task meets_requirements{};
            ex::sender auto task_work = ex::just(task{meets_requirements}) | ex::then([](task&& t){t.run(t);});

            // zmq.send(task_work)
            std::this_thread::sleep_until(next_execution_time);
        }
    };

    auto poller = ex::just(task_mainteriner{}) | ex::then(polling_work);

    // execute the whole flow asynchronously
    ex::start_detached(std::move(poller));


    auto work_manager = [/* zmq */]{
        exec::static_thread_pool work_pool{32};
        ex::scheduler auto work_sched = work_pool.get_scheduler();




    };


    // exec::async_scope scope;
    //
    //
    //
    //
    // scope.spawn(std::move(snd));
    //
    // (void) stdexec::sync_wait(scope.on_empty());
    return 0;
}
