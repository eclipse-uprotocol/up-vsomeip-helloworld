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
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>

#include <sys/time.h>

#include "byteorder.hpp"

#include "hello_proto.h"
#include "hello_utils.h"

namespace HelloExample {

// fwd declarations
void serialize_int32(const int32_t value, std::vector<vsomeip::byte_t>& data);
int32_t deserialize_int32(const vsomeip::byte_t* data, const uint32_t data_size, uint32_t &index);

void serialize_string(const std::string& value, std::vector<vsomeip::byte_t>& serialized);
std::string deserialize_string(const vsomeip::byte_t* data, const uint32_t data_size, uint32_t &index);

template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
    constexpr size_t BUFFER_SIZE = 1024;
    char buf[BUFFER_SIZE];
    int len = std::snprintf(buf, BUFFER_SIZE, format.c_str(), args ...);
    if (len <= 0) {
        return "<ERR>";
    }
    return std::string(buf, buf + len);
}

void serialize_int32(const int32_t value, std::vector<vsomeip::byte_t>& data) {
    data.push_back(VSOMEIP_LONG_BYTE3(value));
    data.push_back(VSOMEIP_LONG_BYTE2(value));
    data.push_back(VSOMEIP_LONG_BYTE1(value));
    data.push_back(VSOMEIP_LONG_BYTE0(value));
}

int32_t deserialize_int32(const vsomeip::byte_t* data, const uint32_t data_size, uint32_t &index) {
    if (index + 4 >= data_size) {
        // handle error
        std::cerr << "FIXME: Invalid index: " << index << ", size: " << data_size << std::endl;
        return -1;
    }
    uint32_t value = VSOMEIP_BYTES_TO_LONG(
        data[index + 0],
        data[index + 1],
        data[index + 2],
        data[index + 3]);

    index = index + 4;
    return value;
}

std::string to_string(const std::vector<vsomeip::byte_t> &data) {
    std::stringstream ss;
    for (uint32_t i = 0; i < data.size(); ++i) {
        ss << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
           << static_cast<int>(data[i]);
        if (i < data.size() - 1) ss << " ";
    }
    return ss.str();
}

void serialize_string(const std::string& value, std::vector<vsomeip::byte_t>& serialized) {
    // 6.2.4.4 Strings (dynamic length)
    // [TR_SOMEIP_00091] d Strings with dynamic length start with a length field. The length
    // is measured in Bytes and is followed by the "\0"-terminated string data
    int str_size = value.size() + 1;
    serialized.reserve(4 + str_size);
    serialize_int32(str_size, serialized);

    // Append the raw bytes from the string
    serialized.insert(serialized.end(), value.begin(), value.end());
    serialized.push_back(0u); // add terminator char: '\x0'
}

std::string deserialize_string(const vsomeip::byte_t* data, const uint32_t data_size, uint32_t &index) {
    if (index + 4 >= data_size) {
        // handle error
        std::cerr << "FIXME: Invalid index: " << index << ", size: " << data_size << std::endl;
        return {};
    }
    int32_t str_size = deserialize_int32(data, data_size, index);
    // sanity checks:
    if (str_size > data_size - 4) {
        std::cerr << "FIXME: Invalid string size: " << str_size << ", size: " << data_size << std::endl;
        return {};
    }
    return std::string(reinterpret_cast<const char*>(data) + index, str_size - 1);

}

bool serialize_hello_request(const HelloRequest& request, std::shared_ptr<vsomeip::payload> payload) {
    // https://www.autosar.org/fileadmin/standards/R23-11/FO/AUTOSAR_FO_PRS_SOMEIPProtocol.pdf
    // Strings should end with \0 char, UTF / Uniucode may be required for Autosar..
#ifdef AUTOSAR_WIRE
    std::vector<vsomeip::byte_t> serialized;
    serialize_string(request.message, serialized);
    payload->set_data(serialized);
#else
    const char *buffer = request.message.c_str();
    payload->set_data(reinterpret_cast<const vsomeip_v3::byte_t*>(buffer), request.message.size() + 1);
#endif
    return true;
}

bool deserialize_hello_request(HelloRequest &request, std::shared_ptr<vsomeip::payload> payload) {
    // Get the data from the payload
    vsomeip::byte_t* payload_data = payload->get_data();
    std::size_t payload_length = payload->get_length();
    // Convert the data to a string (skip thhe ending '\0')
    if (payload_length > 0) {
#ifdef AUTOSAR_WIRE
        uint32_t index = 0;
        request.message = deserialize_string(payload_data, payload_length, index);
#else
        request.message = std::string(reinterpret_cast<char*>(payload_data), payload_length - 1);
#endif
        return true;
    }
    request.message = {};
    return false;
}

std::string to_string(const HelloRequest& request) {
    return request.message;
}

bool serialize_hello_response(const HelloResponse& request, std::shared_ptr<vsomeip::payload> payload) {
#ifdef AUTOSAR_WIRE
    std::vector<vsomeip::byte_t> serialized;
    serialize_string(request.reply, serialized);
    payload->set_data(serialized);
#else
    const vsomeip_v3::byte_t* buffer = reinterpret_cast<const vsomeip_v3::byte_t*>(request.reply.c_str());
    payload->set_data(buffer, request.reply.size() + 1);
#endif
    return true;
}

bool deserialize_hello_response(HelloResponse &response, std::shared_ptr<vsomeip::payload> payload) {
    // Get the data from the payload
    vsomeip::byte_t* payload_data = payload->get_data();
    std::size_t payload_length = payload->get_length();

    // Convert the data to a string (skip thhe ending '\0')
    if (payload_length > 0) {
        response.reply = std::string(reinterpret_cast<char*>(payload_data), payload_length - 1);
        return true;
    }
    response.reply = {};
    return false;
}

std::string to_string(const HelloResponse& response) {
    return response.reply;
}

void init_hello_event(HelloEvent &event) {
    // FIXME: Implementation in Autosar with the same behaviour is likely impossible

    // Get current time
    struct timeval tv;
    ::gettimeofday(&tv, NULL);

    // Convert to local time
    time_t time_now = tv.tv_sec;
    struct tm *now_tm = ::localtime(&time_now); // FIXME: Not threadsafe!

    // Extract hours, minutes, seconds
    event.time_of_day.hours = now_tm->tm_hour;
    event.time_of_day.minutes = now_tm->tm_min;
    event.time_of_day.seconds = now_tm->tm_sec;
    event.time_of_day.nanos = 1000 * tv.tv_usec; // micro to nano sec
}

void set_hello_event(HelloEvent &event, std::chrono::high_resolution_clock::time_point tp) {
    // Convert to local time
    std::time_t currentTime = std::chrono::system_clock::to_time_t(tp);
    // Use localtime_r for thread safety
    std::tm localTime;
    ::localtime_r(&currentTime, &localTime);
    // Get the time in nanoseconds
    auto tp_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(tp);
    long nanoseconds = tp_ns.time_since_epoch().count() % 1000000000;

    event.time_of_day.hours = localTime.tm_hour;
    event.time_of_day.minutes = localTime.tm_min;
    event.time_of_day.seconds = localTime.tm_sec;
    event.time_of_day.nanos = nanoseconds;
}

bool deserialize_hello_event(HelloEvent &event, std::shared_ptr<vsomeip::payload> payload) {
    vsomeip::byte_t* data = payload->get_data();
    vsomeip::length_t size = payload->get_length();
    if (size < HELLO_EVENT_PAYLOAD_SIZE) {
        return false;
    }

    uint32_t index = 0;
    event.time_of_day.hours = deserialize_int32(data, size, index);
    event.time_of_day.minutes = deserialize_int32(data, size, index);
    event.time_of_day.seconds = deserialize_int32(data, size, index);
    event.time_of_day.nanos = deserialize_int32(data, size, index);
    event.timer_id = static_cast<TimerID>(data[index++]);
    return (index == HELLO_EVENT_PAYLOAD_SIZE);
}

bool serialize_hello_event(const HelloEvent& event, std::shared_ptr<vsomeip::payload> payload) {
    std::vector<vsomeip::byte_t> data;
    serialize_int32(event.time_of_day.hours, data);
    serialize_int32(event.time_of_day.minutes, data);
    serialize_int32(event.time_of_day.seconds, data);
    serialize_int32(event.time_of_day.nanos, data);
    data.push_back(static_cast<vsomeip::byte_t>(event.timer_id));
    payload->set_data(data);
//    std::cout << "FIXME: <serialize_hello_event(" << to_string(event) << ") --> " << bytes_to_string(payload->get_data(), payload->get_length()) << std::endl;
    return true;
}

std::string to_string(const TimerID& id) {
    switch (id) {
        case Timer_1sec: return "T_1s";
        case Timer_1min: return "T_1m";
        case Timer_10ms: return "T_10ms";
        case Timer_1ms:  return "T_1ms";
        default: return "T_inv";
    }
}

std::ostream& operator<<(std::ostream& os, const TimerID& id) {
    os << to_string(id);
    return os;
}

int to_int(const TimerID& id) {
    return static_cast<int>(id);
}

std::string to_string(const HelloEvent& event) {
    std::string event_str = "<" + to_string(event.timer_id) + ">";
    std::stringstream ss;
    ss << "HelloEvent "
        << std::setfill(' ') << std::setw(8) << std::left << event_str << " "
        << std::setfill('0') << std::setw(2) << (int)event.time_of_day.hours << ":"
        << std::setfill('0') << std::setw(2) << (int)event.time_of_day.minutes << ":"
        << std::setfill('0') << std::setw(2) << (int)event.time_of_day.seconds << "."
        << std::setw(9) << std::setfill('0') << (unsigned)(event.time_of_day.nanos); // nanos to millis
    return ss.str();
}

std::chrono::time_point<std::chrono::high_resolution_clock> to_time_point(const HelloEvent& event) {
    using namespace std::chrono;
    int64_t ts_nano = event.time_of_day.hours   * duration_cast<nanoseconds>(hours(1)).count() +
                      event.time_of_day.minutes * duration_cast<nanoseconds>(minutes(1)).count() +
                      event.time_of_day.seconds * duration_cast<nanoseconds>(seconds(1)).count() +
                      event.time_of_day.nanos;
    duration<int64_t, std::nano> duration_ns(ts_nano);
    return time_point<high_resolution_clock>(duration_ns);
}

int timer_interval_ms(const TimerID& id) {
    switch (id) {
        case Timer_1ms:  return 1;
        case Timer_10ms: return 10;
        case Timer_1sec: return 1000;
        case Timer_1min: return 60*1000;
    }
    return -1;
}

std::string to_hex(uint32_t value, int padding) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(padding) << std::hex << (unsigned)value;
    return ss.str();
}

std::string bytes_to_string(const vsomeip::byte_t *data, uint32_t length) {
    std::stringstream ss;
    for (uint32_t i = 0; i < length; ++i) {
        ss << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
           << static_cast<int>(data[i]);
        if (i < length - 1) ss << " ";
    }
    return ss.str();
}

std::string to_string(vsomeip::message_type_e msg_type) {
    switch (msg_type) {
        case vsomeip::message_type_e::MT_ERROR:
            return "Error";
         case vsomeip::message_type_e::MT_ERROR_ACK:
            return "Error/ack";
         case vsomeip::message_type_e::MT_NOTIFICATION:
            return "Notification";
         case vsomeip::message_type_e::MT_NOTIFICATION_ACK:
            return "Notification/ack";
         case vsomeip::message_type_e::MT_REQUEST:
            return "Request";
         case vsomeip::message_type_e::MT_REQUEST_ACK:
            return "Request/ack";
         case vsomeip::message_type_e::MT_REQUEST_NO_RETURN:
            return "Request/no_ret";
         case vsomeip::message_type_e::MT_REQUEST_NO_RETURN_ACK:
            return "Request/no_ret/ack";
         case vsomeip::message_type_e::MT_RESPONSE:
            return "Response";
         case vsomeip::message_type_e::MT_RESPONSE_ACK:
            return "Response/ack";
        default:
            std::stringstream its_message;
            its_message << "Unknown <0x" << std::hex << (int)msg_type << ">";
            return its_message.str();
    }
}

std::string to_string(vsomeip::return_code_e rc) {
    switch (rc) {
        case vsomeip::return_code_e::E_OK:
            return "E_OK";
        case vsomeip::return_code_e::E_NOT_OK:
            return "E_NOT_OK";
        case vsomeip::return_code_e::E_UNKNOWN_SERVICE:
            return "E_UNKNOWN_SERVICE";
        case vsomeip::return_code_e::E_UNKNOWN_METHOD:
            return "E_UNKNOWN_METHOD";
        case vsomeip::return_code_e::E_NOT_READY:
            return "E_NOT_READY";
        case vsomeip::return_code_e::E_NOT_REACHABLE:
            return "E_NOT_REACHABLE";
        case vsomeip::return_code_e::E_TIMEOUT:
            return "E_TIMEOUT";
        case vsomeip::return_code_e::E_WRONG_PROTOCOL_VERSION:
            return "E_WRONG_PROTOCOL_VERSION";
        case vsomeip::return_code_e::E_WRONG_INTERFACE_VERSION:
            return "E_WRONG_INTERFACE_VERSION";
        case vsomeip::return_code_e::E_MALFORMED_MESSAGE:
            return "E_MALFORMED_MESSAGE";
        case vsomeip::return_code_e::E_WRONG_MESSAGE_TYPE:
            return "E_WRONG_MESSAGE_TYPE";
        case vsomeip::return_code_e::E_UNKNOWN:
            return "E_UNKNOWN";
        default:
            return "INVALID!";
    }
}

uint32_t get_env_uint32(const std::string &env_name, const uint32_t default_val) {
    const char* env_value = std::getenv(env_name.c_str());
    if (!env_value) return default_val;
    uint32_t value = default_val;
    std::string str_value = env_value;
    try {
        if (str_value.substr(0, 2) == "0x") { // handle "0x" strings
            value = static_cast<uint32_t>(std::stoul(str_value.substr(2), nullptr, 16));
        } else {
            value = static_cast<uint32_t>(std::stoul(str_value));
        }
    } catch (const std::exception& e) {
        std::cerr << __func__ << ": Invalid value for " << env_name << ": '" << str_value << "'" << std::endl;
    }
    return value;
}

uint32_t parse_uint32(const std::string &value) {
    uint32_t result = 0;
    if (value.substr(0, 2) == "0x") { // handle "0x" strings
        result = static_cast<uint32_t>(std::stoul(value.substr(2), nullptr, 16));
    } else {
        result = static_cast<uint32_t>(std::stoul(value));
    }
    return result;
}


std::string print_service(vsomeip::service_t service,
                          vsomeip::instance_t instance)
{
    std::stringstream ss;
    ss << (service == vsomeip::ANY_SERVICE ? "ANY" : to_hex(service)) << "."
       << (instance == vsomeip::ANY_INSTANCE ? "ANY" : to_hex(instance));
    return ss.str();
}

std::string print_service_ver(vsomeip::service_t service,
                          vsomeip::instance_t instance,
                          vsomeip::major_version_t major_version,
                          vsomeip::minor_version_t minor_version)
{
    std::stringstream ss;
    ss << print_service(service, instance) << " v"
       << (major_version == vsomeip::ANY_MAJOR ? "ANY" : std::to_string(static_cast<int>(major_version))) << "."
       << (minor_version == vsomeip::ANY_MINOR ? "ANY" : std::to_string(static_cast<int>(minor_version)));
    return ss.str();
}

} // namespace HelloExample
