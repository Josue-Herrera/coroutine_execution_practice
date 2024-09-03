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
#include <execution>

// Pull in the reference implementation of P2300:
#include <range/v3/all.hpp>
#include <spdlog/spdlog.h>
#include <fmt/ranges.h>
#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>
#include <exec/task.hpp>
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
    constexpr auto enqueue(T&& t) noexcept -> void
    {
        std::lock_guard<std::mutex> lock(m);
        q.push(std::forward<T>(t));
        c.notify_one();
    }

    [[nodiscard]] constexpr auto dequeue() noexcept -> T
    {
        std::unique_lock<std::mutex> lock(m);
        while(q.empty()) c.wait(lock);
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
        auto const next_execution_time = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};

        for(auto const& ts : maintainer.tasks_)
        {
            if (ts.i == 349) finished = true;
            std::this_thread::sleep_for(std::chrono::microseconds{50});
        }

        log->info("[poller] queueing tasks to work thread for the {} time", ++b);
        queue->enqueue
        (
            // we are find the available tasks to run and sending them to work manager thread
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

struct task_result_type { };


auto process_task(task const&) -> task_result_type { return {}; }

auto work_manager (worker_parameters&& parameters) -> void
{
    auto&& [log, queue, finished, i, b ] = std::forward<worker_parameters>(parameters);

    exec::static_thread_pool work_pool{32};
    auto work_scheduler = work_pool.get_scheduler();
    exec::async_scope scope;

    while (not finished)
    {
        if (b++ == 3) finished = true;
        log->info("[manager] waiting for tasks");
        auto const tasks = queue->dequeue();
        // alert the user => task in progress
        log->info("[manager] received {} tasks", std::size(tasks));

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

/**
 * purpose:
 *     1. waits for tasks in queue
 *     2. spawns processes in thread_pool
 *     3. wait for all tasks to finish
 *     --- 4. accumulate results of process
 */
auto work_sender (ex::scheduler auto& work_scheduler, auto file_logger, auto task_queue)
{
    return ex::on(work_scheduler,
            ex::just(worker_parameters{file_logger, task_queue} )
            | ex::then(work_manager)
    );
}

// coroutine
// support API commands
struct dag_create { int id{};};
struct dag_delete { int id{};};
struct dag_run{int id{};};
struct dag_snapshot {int id{};};

struct task_create {int id{};};
struct task_delete {int id{};};
struct task_run {int id{};};
struct command_error{int id{};};

struct command {
    using input_commands_t = std::variant
    <
        std::monostate,
        dag_create,
        dag_delete,
        dag_run,
        dag_snapshot,
        task_create,
        task_delete,
        task_run,
        command_error
    >;

    input_commands_t command_{};
};

auto zmq() -> void
{
    // setup zmq context, sockets, polling.
    // buffer
    while (true) {
        // auto some_data = socket.get(buffer);
        // co_yeild to_command(some_data);
    }
    //co_return {};
};

struct command_result_type {};
struct notification_type {
    std::chrono::steady_clock::time_point time;
    int associated_dag;
};
 class concurrent_shy_guy
{
public:
     /**
      * @brief Create DAG in a concurrent manner
      * @param input
      */
     auto process_command(dag_create input) noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("dag info {}", input.id);
         return {};
     }

     /**
      * @brief Delete DAG in a concurrent manner
      * @param input
      */
     auto process_command(dag_delete input) noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("dag delete  {}", input.id);
         return {};
     }

     /**
      * @brief Snapshop DAG in a concurrent manner
      * @param input
      */
     auto process_command(dag_snapshot input) const noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("dag snapshot {}", input.id);
         return {};
     }

     /**
      * @brief Create DAG in a concurrent manner
      * @param input
      */
     auto process_command(dag_run input) noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("dag run {}", input.id);
         return {};
     }

     /**
     * @brief Create Task in a concurrent manner
     * @param input
     */
     auto process_command(task_create input) noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("task create {}", input.id);
         return {};
     }

     /**
     * @brief delete Task in a concurrent manner
     * @param input
     */
     auto process_command(task_delete input) noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("task create {}", input.id);
         return {};
     }

     /**
      * @brief run Task in a concurrent manner
      * @param input copied value to prevent spooky action at a distance
      */
     auto process_command(task_run input) const noexcept -> command_result_type {
         std::lock_guard<std::mutex> lock (this->mutex_);
         log->info("task run {}", input.id);
         return {};
     }

     /**
      * @brief run next scheduled dag
      *
      * @return time of the next scheduled dag.
      */
     auto next_scheduled_dag() const noexcept -> std::optional<notification_type> {
         std::lock_guard<std::mutex> lock (this->mutex_);
         return { notification_type {
             .time = std::chrono::steady_clock::now() +  std::chrono::seconds{5},
             .associated_dag = 1
         }};
     }
private:
     mutable std::mutex mutex_{};
     std::shared_ptr<spdlog::logger> log;
};

class notify_updater
{
public:

    auto notify_wakeup(notification_type notification) noexcept -> void {
        std::lock_guard<std::mutex> lock(this->mutex_);
        cached_time_ = notification;
        this->condition_variable_.notify_one();
    }

    auto sleep_until_or_notified(std::chrono::steady_clock::time_point next_time) noexcept {
        std::unique_lock<std::mutex> lock {this->mutex_};
        this->condition_variable_.wait_until(lock, next_time);
        auto result_time = cached_time_;
        cached_time_.reset();
        return result_time;
    }

private:
    std::optional<notification_type> cached_time_{};
    mutable std::mutex mutex_{};
    std::condition_variable condition_variable_{};
};


auto function_schedule_runner (notify_updater& notifier, concurrent_shy_guy& shy_guy) {
    // default sleep time
    auto sleep_until_dag_scheduled = [&] mutable -> notification_type
    {
        auto default_wait_time = std::chrono::steady_clock::now() + std::chrono::months(1);
            while(true)
            {
                if (auto notification  = notifier.sleep_until_or_notified(default_wait_time); notification.has_value())
                {
                    return notification.value();
                }
                else
                {
                    default_wait_time += std::chrono::months(1);
                }
            }
    };

    auto launch_dag_and_sleep_till_next = [&] (auto notified) mutable
    {
        while (true)
        {
            if (auto notification = notifier.sleep_until_or_notified(notified.time); notification.has_value())
            {
                notified = notification.value();
                continue;
            }

            shy_guy.process_command(dag_run{notified.associated_dag});

            auto const next_scheduled_time = shy_guy.next_scheduled_dag();
            if (not next_scheduled_time.has_value())
                break;

            notified = next_scheduled_time.value();
        }
    };

    while (true)
    {
        auto const notification = sleep_until_dag_scheduled();
        launch_dag_and_sleep_till_next(notification);
    }
}

auto main() -> int
{
    auto file_logger    = spdlog::basic_logger_mt("file_log", "logs/file-log.txt", true);
    auto task_queue     = std::make_shared<blocking_queue<std::vector<task>>>();
//  auto io_queue       = std::make_shared<blocking_queue<std::vector<task>>>();
    auto work_pool      = exec::static_thread_pool{3};
    auto work_scheduler = work_pool.get_scheduler();
    auto scope          = exec::async_scope{};
//1. auto dependency_map = concurrent_map<string, dags>{}
//2. auto task_maintainer = concurrent_task_maintainer{};

    ex::sender auto worker = work_sender(work_scheduler, file_logger, task_queue);
    scope.spawn(std::move(worker));

    scope.spawn(
        ex::on(work_scheduler,
            ex::just(poll_parameters{file_logger, task_queue})
            | ex::then(polling_work)
        )
    );

    scope.spawn(
         ex::on(work_scheduler,
            ex::just()
            | ex::then([] {
                while (false) {
                    //auto value = co_await zmq();
                    //io_queue.enqueue(co_await zmq());
                }
            })
        )
    );

    (void) stdexec::sync_wait(scope.on_empty());
    work_pool.request_stop();
    return 0;
}


// for each dag -> have a schedule in which it will sleep and then from there
// like if dag 1 (every hour) and dag 2 (every 45 mins) starting at 1pm
// calculate the dag with the minimum difference in time (next closes time) ((dag2) 1:45pm is earlier than (dag1) 2:00pm)
// api calculate_next_time_to_sleep(vector<tuple<name, schedule, last_time_ran>>, time_now) -> tuple { next_dag_to_run_name,
// sleep till 1:45 and then start dag 2
//
//