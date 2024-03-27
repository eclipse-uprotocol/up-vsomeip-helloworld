/*
 * Copyright (c) 2024 Contributors to the Eclipse Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <pthread.h>

#include <cmath>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>

#include "timer.h"
#include "hello_utils.h"

namespace HelloExample {

static int debug = ::getenv("TIMER_DEBUG") ? ::atoi(::getenv("TIMER_DEBUG")) : 0;
static int timer_cb_max_us = ::getenv("TIMER_CB_US") ? ::atoi(::getenv("TIMER_CB_US")) : 0;

Timer::Timer() :
    is_running_(true)
{
}

Timer::~Timer() {
    stop_timers();
    if (debug > 0) std::printf("  // Timer::~Timer(): stopping %lu threads\n", threads_.size());
    for (auto &thread : threads_) {
        if (std::this_thread::get_id() == thread.get_id()) {
            if (debug > 1) std::cout << "  // Timer::~Timer(): detaching thread: " << std::hex << thread.get_id() << std::endl;
            thread.detach();
        } else if (thread.joinable()) {
            if (debug > 1) std::cout << "  // Timer::~Timer(): joining thread: " << std::hex << thread.get_id() << std::endl;
            thread.join();
        }
    }
    threads_.clear();
}

void Timer::add_timer(timer_callback callback, int timer_id, int interval, bool recurring) {
    auto th = std::thread(
            std::bind(&Timer::timer_thread, this,
                    callback, timer_id, interval, recurring));
    std::string th_name = "timer_" + std::to_string(timer_id);
    pthread_setname_np(th.native_handle(), th_name.c_str());
    threads_.push_back(std::move(th));
}

void Timer::stop_timers() {
    if (debug > 0) std::printf("  // Timer::stop_timers(): is_running: %d\n", is_running_.load());
    // std::unique_lock<std::mutex> lock(mtx_);
    // if ((!is_running_ && threads_.size() == 0)) {
    //     return;
    // }
    is_running_ = false;
    cv_.notify_all(); // notify all waiting threads
}

void Timer::timer_thread(timer_callback callback, int timer_id, int interval, bool recurring) {
    if (debug > 0) std::printf("  // timer_thread[id=%d,timeout=%d] started.\n", timer_id, interval);
    const int64_t default_interval = interval * 1000L; // in us
    int64_t to_wait_us = default_interval;
    do {
        // wait for the specified interval or until stop() is called
        std::unique_lock<std::mutex> lock(mtx_);
        if (debug > 1) std::printf("  // timer_thread[id=%d,timeout=%d] waiting(%ld us)....\n", timer_id, interval, to_wait_us);
        if (cv_.wait_for(lock,
                std::chrono::microseconds(to_wait_us), [=] {
                    return !is_running_;
                } )) {
            if (debug > 0) std::printf("  // timer_thread[id=%d,timeout=%d] stop requested.\n", timer_id, interval);
            break;
        }
        if (!is_running_) break;
        //if (debug > 2) std::printf("  // timer_thread[id=%d,timeout=%d] cb().\n", timer_id, interval);
        auto ts = std::chrono::high_resolution_clock::now();
        callback(timer_id); // call the user callback
        std::chrono::duration<double, std::micro> elapsed = std::chrono::high_resolution_clock::now() - ts;
        to_wait_us = default_interval - elapsed.count();
        if (debug > 1) std::printf("  // timer_thread[id=%d,timeout=%d] cb took %.4f ms\n",
            timer_id, interval, (elapsed.count()) / 1000.0);
        if (to_wait_us < 1) {
            to_wait_us = 1; // WARNING: busy loop!
        }
        if (timer_cb_max_us > 0 && elapsed.count() > timer_cb_max_us) {
            std::printf("  %s/!\\%s timer_thread[id=%d,timeout=%-4d] callback delayed for: %7.3f ms, next wait: %7.3f ms.\n",
                COL_RED.c_str(), COL_NONE.c_str(),
                timer_id, interval, (elapsed.count()) / 1000.0, to_wait_us / 1000.0);
        }
    } while (recurring);
    if (debug > 0) std::printf("  // timer_thread[id=%d,timeout=%d] finished.\n", timer_id, interval);
}

} // namespace HelloExample
