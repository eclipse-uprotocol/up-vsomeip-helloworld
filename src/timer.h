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
#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <condition_variable>

namespace HelloExample {

typedef std::function<void(int timer_id)> timer_callback;

class Timer {
public:
    Timer();
    ~Timer();

    void add_timer(timer_callback callback, int timer_id, int interval, bool recurring=false);
    void stop_timers();

private:
    void timer_thread(timer_callback callback, int timer_id, int interval, bool recurring);

    std::vector<std::thread> threads_;
    std::atomic<bool> is_running_;
    std::condition_variable cv_;
    std::mutex mtx_;
};

} // namespace HelloExample
