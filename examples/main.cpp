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
#include <mutex>
#include <queue>

// Pull in the reference implementation of P2300:
#include <range/v3/all.hpp>
#include <spdlog/spdlog.h>
#include <fmt/ranges.h>
#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>
#include <range/v3/all.hpp>
// Use a thread pool
#include "exec/static_thread_pool.hpp"
#include "spdlog/sinks/basic_file_sink.h"

namespace ex = stdexec;
struct task {
    std::string name_;
    int i;
    void run(task const&){}
    void error(std::exception_ptr err){  std::rethrow_exception(err); }
    void stopped(){}
    void write_to_file(){}
};

struct task_maintainer {
    std::vector<task> tasks_;
};

template <class T> struct blocking_queue
{
    constexpr void enqueue(T&& t) noexcept
    {
        std::lock_guard<std::mutex> lock(m);
        q.push(std::forward<T>(t));
        c.notify_one();
    }

    [[nodiscard]] constexpr T dequeue() noexcept
    {
        std::unique_lock<std::mutex> lock(m);
        while(q.empty()) { c.wait(lock); }
        T val = std::move(q.front());
        q.pop();
        return val;
    }

private:
    std::queue<T> q{};
    mutable std::mutex m{};
    std::condition_variable c{};
};

struct poll_parameters {
    std::shared_ptr<spdlog::logger> log;
    std::shared_ptr<blocking_queue<std::vector<task>>> queue;
    task_maintainer maintainer{};
    bool finished = false;
};
auto polling_work (poll_parameters&& params) -> void
{
    auto&& [log, queue, maintainer, finished] = std::forward<poll_parameters>(params);

    maintainer.tasks_ = ranges::views::iota(1, 50)
                            | ranges::views::transform([](auto i){ return task {std::to_string(i) + " : task id", i }; })
                            | ranges::to<std::vector>();
    auto b = 0;
    while (not finished)
    {
        auto next_execution_time = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};

        for(auto const& ts : maintainer.tasks_)
        {
            if (ts.i == 349) finished = true;
            std::this_thread::sleep_for(std::chrono::microseconds{50});
        }

        log->info("[poller] queueing tasks to work thread for the {} time", ++b);
        queue->enqueue
        (
            maintainer.tasks_
            | ranges::views::filter([](task const& t){return t.i % 2 == 0;})
            | ranges::to<std::vector>()
        );

        for(auto& ts : maintainer.tasks_) ts.i += 100;


        std::this_thread::sleep_until(next_execution_time);
    }
};

struct worker_parameters {
    std::shared_ptr<spdlog::logger> log;
    std::shared_ptr<blocking_queue<std::vector<task>>> queue;
    bool finished = false;
    int i = 1, b = 0;
};

auto work_manager (worker_parameters&& parameters) -> void
{
    auto [log, queue, finished, i, b ] = parameters;
    exec::static_thread_pool work_pool{32};
    auto work_scheduler = work_pool.get_scheduler();
    exec::async_scope scope;

    while (not finished)
    {
        if (b++ == 3) finished = true;
        log->info("[manager] waiting for tasks");
        auto const tasks = queue->dequeue();
        log->info("[manager] recieved {} tasks", std::size(tasks));

        for (auto const& t : tasks)
            scope.spawn(ex::on(work_scheduler,
                ex::just(t, i++)
                 | ex::then([log](task const& xt, int xi) {
                     log->info("[worker:{}] working on task={}",xi, xt.i);
                     std::this_thread::sleep_for(std::chrono::microseconds{50});
                 }))
            );
        log->info("[manager] launching tasks for {} time", b);
    }

    (void) stdexec::sync_wait(scope.on_empty());
    log->info("[manager] finished all tasks");
};




auto main() -> int {
    auto file_logger = spdlog::basic_logger_mt("file_log", "logs/file-log.txt", true);
    auto task_queue     = std::make_shared<blocking_queue<std::vector<task>>>();
    auto work_pool      = exec::static_thread_pool{2};
    auto work_scheduler = work_pool.get_scheduler();
    auto scope          = exec::async_scope{};

    scope.spawn(
    ex::on(work_scheduler,
            ex::just(worker_parameters{file_logger, task_queue} )
            | ex::then(work_manager)
        )
    );

    scope.spawn(
        ex::on(work_scheduler,
            ex::just(poll_parameters{file_logger, task_queue})
            | ex::then(polling_work)
        )
    );

    (void) stdexec::sync_wait(scope.on_empty());
    work_pool.request_stop();
    return 0;
}
