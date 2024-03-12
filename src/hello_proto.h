/********************************************************************************
 * Copyright (c) 2024 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
#pragma once

#include <stdint.h>
#include <string>

#define HELLO_SERVICE_ID       0x6000
#define HELLO_INSTANCE_ID      0x0001
#define HELLO_METHOD_ID        0x8001

#define HELLO_EVENTGROUP_ID    0x0100
#define HELLO_EVENT_ID         0x8005

/// IMPORTANT: should match vsomeip::DEFAULT_MAJOR in (interface/vsomeip/constants.hpp):
// but Autosar works better if vsomeip::DEFAULT_MAJOR is 1 (thus Autosar needs custom vsomeip build)
#define HELLO_SERVICE_MAJOR    (int)(vsomeip::DEFAULT_MAJOR) // 1u
#define HELLO_SERVICE_MINOR    0u

// #define AUTOSAR_WIRE    // [experimental] If defined use dynamic string length (uint32_t) before string payload

namespace HelloExample {

struct HelloRequest {
    std::string message;
};

struct HelloResponse {
    std::string reply;
};

enum TimerID {
    Timer_1sec = 0,
    Timer_1min = 1,
    // FIXME: sync values with uservice hello world example
    Timer_10ms = 8,
    Timer_1ms =  9,
};

// FIXME: not a good match for Autosar, it has microsecond timers only

// Represents a time of day. The date and time zone are either not significant
struct TimeOfDay {
  // Hours of day in 24 hour format [0..23]
  int32_t hours;
  // Minutes of hour of day [0..59].
  int32_t minutes;
  // Seconds of minutes of the time [0..59]
  int32_t seconds;
  // Fractions of seconds in nanoseconds [0..999,999,999].
  int32_t nanos;
};

struct HelloEvent {
    TimeOfDay time_of_day;
    TimerID timer_id;
};

constexpr uint32_t HELLO_EVENT_PAYLOAD_SIZE = 17;

} // namespace HelloExample
