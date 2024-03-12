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

#include <chrono>

#include <stdint.h>
#include <vsomeip/vsomeip.hpp>
#include "hello_proto.h"

static const std::string COL_NONE = "\033[0m";
static const std::string COL_RED = "\033[0;31m";
static const std::string COL_BLUE = "\033[0;34m";
static const std::string COL_YELLOW = "\033[0;33m";
static const std::string COL_WHITE = "\033[0;37m";
static const std::string COL_WHITE_BOLD = "\033[1;37m";
static const std::string COL_GREEN = "\033[0;32m";

namespace HelloExample {

bool serialize_hello_request(const HelloRequest& request, std::shared_ptr<vsomeip::payload> payload);
bool deserialize_hello_request(HelloRequest &request, std::shared_ptr<vsomeip::payload> payload);
std::string to_string(const HelloRequest& request);

bool serialize_hello_response(const HelloResponse& request, std::shared_ptr<vsomeip::payload> payload);
bool deserialize_hello_response(HelloResponse &request, std::shared_ptr<vsomeip::payload> payload);
std::string to_string(const HelloResponse& request);

void init_hello_event(HelloEvent &event);
void set_hello_event(HelloEvent &event,
        std::chrono::high_resolution_clock::time_point tp = std::chrono::high_resolution_clock::now());

bool serialize_hello_event(const HelloEvent& event, std::shared_ptr<vsomeip::payload> payload);
bool deserialize_hello_event(HelloEvent &event, std::shared_ptr<vsomeip::payload> payload);
std::string to_string(const HelloEvent& request);
std::ostream& operator<<(std::ostream& os, const TimerID& id);
std::chrono::time_point<std::chrono::high_resolution_clock> to_time_point(const HelloEvent& event);
int timer_interval_ms(const TimerID& id);

std::string to_string(const TimerID& id);
int to_int(const TimerID& id);

std::string to_string(vsomeip::return_code_e rc);

std::string to_string(const std::vector<vsomeip::byte_t> &data);
std::string bytes_to_string(const vsomeip::byte_t *data, uint32_t length);
std::string to_string(vsomeip::message_type_e msg_type);
std::string to_hex(uint32_t value, int padding=4);

} // namespace HelloExample
