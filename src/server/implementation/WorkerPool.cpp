/*
 * (C) Copyright 2014 Kurento (http://kurento.org/)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "WorkerPool.hpp"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <gst/gst.h>

#include <atomic>
#include <memory>

// The health checker method `WorkerPool::checkThreads()` runs as a task in the
// worker thread pool, in order to monitor if task scheduling is lagging behind
// the expected time. This interval defines the time elapsed between checks.
// Finding that the thread loop is lagging means that there are too many tasks
// scheduled at the same time, and the thread pool is not able to cope with all
// of them. For now, we only warn about it, as there is not much more we can do.
static const auto THREAD_CHECK_INTERVAL = std::chrono::seconds(5);

#define GST_CAT_DEFAULT kurento_worker_pool
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "KurentoWorkerPool"

namespace kurento
{

WorkerPool::WorkerPool (size_t threads_count)
  : io_work {io_service}
  , check_timer {io_service}
  , check_time_last {std::chrono::steady_clock::now()}
{
  // Add threads to the thread pool
  if (threads_count == 0) {
    // Use as many threads as CPU cores exist in the current environment
    threads_count = boost::thread::hardware_concurrency ();

    // If hardware_concurrency() returns 0, fall back to 1 thread
    threads_count = threads_count > 0 ? threads_count : 1;
  }
  for (size_t t = 0; t < threads_count; ++t) {
    io_threadpool.create_thread (
        boost::bind (&boost::asio::io_service::run, &io_service));
  }

  // Thread pool health checker
  check_timer.expires_from_now(THREAD_CHECK_INTERVAL);
  check_timer.async_wait (boost::bind (&kurento::WorkerPool::checkThreads, this));
}

WorkerPool::~WorkerPool ()
{
  // Calling `io_service->stop()` causes the `io_service.run()` method to return
  // from its internal loop, also preventing any new tasks from being assigned
  // to the thread pool.
  // io_service->stop ();
  io_service.stop ();

  // Wait until all the threads in the thread pool are finished with their
  // currently assigned tasks, and "join" them. Just assume the threads inside
  // the threadpool will be destroyed by this method.
  io_threadpool.join_all ();
}

void
WorkerPool::checkThreads ()
{
  // Compare the expected time between triggers with the actual time elapsed
  // since last trigger happened. If the later is higher, it means that the
  // thread pool is saturated and tasks are being run with a big delay!
  const std::chrono::steady_clock::time_point check_time_now = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::duration check_time_diff = check_time_now - check_time_last;

  // `steady_clock::duration` measures time in seconds
  // Multiply by 1.1 to allow for some margin in the comparison
  if (check_time_diff > (THREAD_CHECK_INTERVAL * 1.1)) {
    GST_WARNING ("Worker thread pool is lagging! (CPU exhausted?)");
  }

  // Update control variable for the next run, with current time
  check_time_last = std::chrono::steady_clock::now();

  // Reset the timer to run again later
  check_timer.expires_from_now(THREAD_CHECK_INTERVAL);
  check_timer.async_wait(boost::bind(&kurento::WorkerPool::checkThreads, this));
}

WorkerPool::StaticConstructor WorkerPool::staticConstructor;

WorkerPool::StaticConstructor::StaticConstructor ()
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
                           GST_DEFAULT_NAME);
}

} // kurento
