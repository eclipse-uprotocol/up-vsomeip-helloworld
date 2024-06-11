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
#include <csignal>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>

#include <vsomeip/vsomeip.hpp>

#include "hello_proto.h"
#include "hello_utils.h"

// suppresses periodic LOG_INFO,LOG_DEBUG,LOG_TRACE messages
static int quiet = ::getenv("QUIET") ? ::atoi(::getenv("QUIET")) : 0;
// debug dumps (shall be disabled by QUIET=1)
static int debug = ::getenv("DEBUG") ? ::atoi(::getenv("DEBUG")) : 1;
// delay (ms) after sending a hello request from the request thread
static int delay = ::getenv("DELAY") ? ::atoi(::getenv("DELAY")) : 0;

// time difference from previous timer interval to dump warnings for dalay
static int max_delta = ::getenv("DELTA") ? ::atoi(::getenv("DELTA")) : 0;

static const std::string P_ERROR = COL_YELLOW + "[HelloCli] " + COL_RED;
static const std::string P_INFO  = COL_YELLOW + "[HelloCli] " + COL_WHITE_BOLD;
static const std::string P_DEBUG = COL_YELLOW + "[HelloCli] " + COL_NONE;
static const std::string P_TRACE = COL_YELLOW + "[HelloCli] " + COL_BLUE;

#define LOG_TRACE  std::cout << P_TRACE
#define LOG_DEBUG  std::cout << P_DEBUG
#define LOG_INFO   std::cout << P_INFO
#define LOG_ERROR  std::cerr << P_ERROR

// terminate log msg (reset colors and flush)
#define LOG_CR       COL_NONE << std::endl

namespace HelloExample {

struct client_config_t {
    vsomeip::service_t service_id;
    vsomeip::instance_t instance_id;
    vsomeip::major_version_t major_version;
    vsomeip::minor_version_t minor_version;
    vsomeip::method_t method_id;
    vsomeip::eventgroup_t eventgroup_id;
    vsomeip::event_t event_id;
};

client_config_t DEFAULT_CONFIG = {
    HELLO_SERVICE_ID,
    HELLO_INSTANCE_ID,
    HELLO_SERVICE_MAJOR,
    HELLO_SERVICE_MINOR,
    HELLO_METHOD_ID,
    HELLO_EVENTGROUP_ID,
    HELLO_EVENT_ID
};

class hello_client {

private:
    std::shared_ptr<vsomeip::application> app_;
    client_config_t config_;
    bool use_tcp_; // TCP or UDP endpoint config
    bool subscribe_events_; // Subscribe for Hello Timer events
    bool is_registered_;
    bool is_available_; // HelloService available
    int request_count_; // how many calls to sayHello()

    HelloRequest hello_req_;

    bool initialized_; // is_offered_ must be initialized before starting the threads!
    bool running_;

    std::mutex mutex_;
    std::condition_variable condition_;

    std::mutex request_mutex_;
    std::condition_variable request_condition_;
    vsomeip::request_t hello_req_id_;
    HelloResponse hello_resp_;

    std::thread request_thread_; // hendles hello request sending loop

    // benchmarking
    std::map<TimerID,int32_t> event_counters_;
    std::map<TimerID,std::chrono::time_point<std::chrono::high_resolution_clock>> last_event_;
    std::chrono::time_point<std::chrono::high_resolution_clock> ts_event_;

    int32_t requests_sent_;
    std::chrono::time_point<std::chrono::high_resolution_clock> ts_req_start_;
    std::chrono::time_point<std::chrono::high_resolution_clock> ts_req_finish_;


public:
    hello_client(client_config_t _config, bool _use_tcp, bool _subscribe_events, HelloRequest& _hello_req, int _request_count)
        : app_(vsomeip::runtime::get()->create_application())
        , config_(_config)
        , use_tcp_(_use_tcp)
        , request_count_(_request_count)
        , hello_req_(_hello_req)
        , subscribe_events_(_subscribe_events)
        , is_registered_(false)
        , is_available_(false)
        , initialized_(false)
        , running_(true)
        , event_counters_()
        , requests_sent_(0)
        , request_thread_(std::bind(&hello_client::run, this))
    {
        pthread_setname_np(request_thread_.native_handle(), "request_thread");
    }

    bool init() {
        std::lock_guard<std::mutex> its_lock(mutex_);
        if (!app_->init()) {
            LOG_ERROR << "Couldn't initialize application" << LOG_CR;
            return false;
        }
        LOG_INFO << "### Hello Client settings ["
                << "cli_id=0x" << to_hex(app_->get_client())
                << ", app='" << app_->get_name()
                << "', protocol=" << (use_tcp_ ? "TCP" : "UDP")
                << ", subscribe_events=" << subscribe_events_
                << ", req_count=" << request_count_
                << ", hello='" << hello_req_.message
                << "', routing=" << app_->is_routing()
                << "]" << LOG_CR;

        // reset counters
        reset_counters();
        app_->register_state_handler(
            std::bind(&hello_client::on_state, this,
                std::placeholders::_1));

        app_->register_message_handler(
                // config_.service_id, config_.instance_id, config_.method_id,
                vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
                std::bind(&hello_client::on_message, this,
                        std::placeholders::_1));

        app_->register_availability_handler(
                config_.service_id, config_.instance_id,
                std::bind(&hello_client::on_availability, this,
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                vsomeip::ANY_MAJOR, vsomeip::ANY_MINOR);
                //config_.major_version, vsomeip::ANY_MINOR);

        app_->register_routing_state_handler(
            std::bind(&hello_client::on_routing_state_changed, this,
                        std::placeholders::_1));

        // register subscription status handler (to handle subscription NACK)
        app_->register_subscription_status_handler(
                config_.service_id, config_.instance_id,
                config_.eventgroup_id, config_.event_id,
                std::bind(&hello_client::on_subscription_status_changed, this,
                          std::placeholders::_1, std::placeholders::_2,
                          std::placeholders::_3, std::placeholders::_4,
                          std::placeholders::_5));

        if (debug > 5) LOG_TRACE << "// [init] initialized=true" << LOG_CR;
        initialized_ = true;
        condition_.notify_one();

        return true;
    }

    void on_routing_state_changed(vsomeip::routing_state_e state) {
        LOG_INFO << "[on_routing_state_changed]" << (int)state << LOG_CR;
    }

    void reset_counters() {
        event_counters_[Timer_1ms] = 0;
        event_counters_[Timer_10ms] = 0;
        event_counters_[Timer_1sec] = 0;
        event_counters_[Timer_1min] = 0;

        last_event_[Timer_1ms] = {};
        last_event_[Timer_10ms] = {};
        last_event_[Timer_1sec] = {};
        last_event_[Timer_1min] = {};

        ts_event_ = std::chrono::high_resolution_clock::now();
    }

    void start() {
        // start() is blocking current thread
        app_->start();
    }


    void print_request_summary() {
        if (requests_sent_ > 0) {
            std::chrono::duration<double, std::milli> diff = ts_req_finish_ - ts_req_start_;
            LOG_INFO << LOG_CR;
            LOG_INFO << "### Sent " << requests_sent_ << " Hello requests for "
                    << std::fixed << std::setprecision(4) << diff.count() << " ms. ("
                    << std::fixed << std::setprecision(4)
                    << (requests_sent_ > 0 ? diff.count() / (double)requests_sent_ : 0.0)
                    << " ms/req)." << LOG_CR;
            LOG_INFO << LOG_CR;
        }
    }

    void print_event_summary(const std::chrono::time_point<std::chrono::high_resolution_clock>& ts) {
        if (!subscribe_events_) {
            return;
        }
        std::chrono::duration<double, std::milli> event_time = ts - ts_event_;
        LOG_INFO << LOG_CR;
        LOG_INFO << "### Received HelloEvents (for "
                << std::fixed << std::setprecision(4) << event_time.count()
                << " ms)" << LOG_CR;
        if (event_counters_[Timer_1ms] > 0) {
            int expected = (int)event_time.count();
            int percent = expected > 0 ? 100 * event_counters_[Timer_1ms] / expected : 0;
            LOG_INFO << "  - Event[Timer_1ms]  = " << std::left << std::setw(6) << std::setfill(' ') << event_counters_[Timer_1ms]
                    << " (expected: " << std::setw(6) << std::setfill(' ') << expected
                    << " " << std::right << std::setw(3) << std::setfill(' ') << percent << "%)" << LOG_CR;
        }
        if (event_counters_[Timer_10ms] > 0) {
            int expected = event_time.count() / 10;
            int percent = expected > 0 ? 100 * event_counters_[Timer_10ms] / expected : 0;
            LOG_INFO << "  - Event[Timer_10ms] = " << std::left << std::setw(6) << std::setfill(' ') << event_counters_[Timer_10ms]
                    << " (expected: " << std::setw(6) << std::setfill(' ') << expected
                    << " " << std::right << std::setw(3) << std::setfill(' ') << percent << "%)" << LOG_CR;
        }
        if (event_counters_[Timer_1sec] > 0) {
            int expected = event_time.count() / 1000;
            int percent = expected > 0 ? 100 * event_counters_[Timer_1sec] / expected : 0;
            LOG_INFO << "  - Event[Timer_1sec] = " << std::left << std::setw(6) << std::setfill(' ') << event_counters_[Timer_1sec]
                    << " (expected: " << std::setw(6) << std::setfill(' ') << expected
                    << " " << std::right << std::setw(3) << std::setfill(' ') << percent << "%)" << LOG_CR;
        }
        if (event_counters_[Timer_1min] > 0) {
            int expected = event_time.count() / 60000;
            int percent = expected > 0 ? 100 * event_counters_[Timer_1min] / expected : 0;
            LOG_INFO << "  - Event[Timer_1min] = " << std::left << std::setw(6) << std::setfill(' ') << event_counters_[Timer_1min]
                    << " (expected: " << std::setw(6) << std::setfill(' ') << expected
                    << " " << std::right << std::setw(3) << std::setfill(' ') << percent << "%)" << LOG_CR;
        }
        LOG_INFO << LOG_CR;
    }

    /*
     * Handle signal to shutdown
     */
    void stop() {
        if (debug > 0) LOG_DEBUG << "Stopping..." << LOG_CR;
        running_ = false;
        initialized_ = true; // don't wait for initialization
        condition_.notify_one();
        request_condition_.notify_one();
        app_->clear_all_handler();

        auto ts_stopped = std::chrono::high_resolution_clock::now();

        if (subscribe_events_) {
            // cleanup event service
            if (debug > 0) LOG_DEBUG << "Unsubscribing HelloService events..." << LOG_CR;
            app_->unsubscribe(config_.service_id, config_.instance_id, config_.eventgroup_id);
            app_->release_event(config_.service_id, config_.instance_id, config_.event_id);
        }
        app_->release_service(config_.service_id, config_.instance_id);
        if (std::this_thread::get_id() == request_thread_.get_id()) {
            if (debug > 1) LOG_TRACE << "Detaching request_thread..." << LOG_CR;
            request_thread_.detach();
        } else if (request_thread_.joinable()) {
            if (debug > 1) LOG_TRACE << "Joining request_thread..." << LOG_CR;
            request_thread_.join();
        }
        if (debug > 1) LOG_TRACE << "app->stop()..." << LOG_CR;
        app_->stop();

        // event benchmarks
        print_event_summary(ts_stopped);
        // repeat request summary (could be lost in scrollback)
        // if (subscribe_events_ && request_count_ > 0)
        {
            print_request_summary();
        }
    }

    void on_state(vsomeip::state_type_e _state) {
        // FIXME: on_state() may be called multiple times in case of a proxy client, when the router is restarted.
        // It may be needed to request the service/events again.
        if (_state == vsomeip::state_type_e::ST_REGISTERED) {
            is_registered_ = true;
            if (debug > 0) LOG_DEBUG << "[on_state] ST_REGISTERED." << LOG_CR;
            LOG_INFO << "[on_state] Requesting Hello Service ["
                    << print_service_ver(config_.service_id, config_.instance_id,
                                         config_.major_version, config_.minor_version)
                    << "]" << LOG_CR;
            // FIXME: event service_id/instance_id could be different than request/response service_id/instance_id. Use dedicated event config.
            app_->request_service(config_.service_id, config_.instance_id, config_.major_version, config_.minor_version);
        } else {
            if (debug > 0) LOG_DEBUG << "[on_state] ST_DEREGISTERED." << LOG_CR;
        }
    }

    void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
        if (debug > 4) {
            LOG_TRACE << "// [on_availability] service:[" << print_service(_service, _instance)
                << "], " << (_is_available ? "Available" : "NOT available")
                << ", config:[" << print_service(config_.service_id, config_.instance_id) << "]"
                << LOG_CR;
        }
        // NOTE: callback is called initially for each find_service(_service, _instance) (including ANY!)
        //       with _is_available: false
        if (_service == vsomeip::ANY_SERVICE && _instance == vsomeip::ANY_INSTANCE) {
            return;
        }
        // smart hadnling of INSTANCE_ANY discovery.. 1st discovered instance will be assumed:
        // This is required as sending events to ANY_INSTANCE will not work.

        // TODO: shall we accept 1st offered service if we requested any service?
        if (_is_available && config_.service_id == vsomeip::ANY_SERVICE) {
            // only reject if configured instance id does not match
            if (config_.instance_id == _instance || config_.instance_id == vsomeip::ANY_INSTANCE) {
                LOG_INFO << "[on_availability] Matched [" << print_service(config_.service_id, config_.instance_id)
                         << "] to incoming Service [" << print_service(_service, _instance) << "]" << LOG_CR;
                config_.service_id = _service;
                config_.instance_id = _instance;
            }
        }
        // allow any instance to match on available instance
        if (_is_available && _service == config_.service_id && config_.instance_id == vsomeip::ANY_INSTANCE) {
            LOG_INFO << "[on_availability] Matched [" << print_service(config_.service_id, config_.instance_id)
                        << "] to incoming Service [" << print_service(_service, _instance) << "]" << LOG_CR;
            config_.instance_id = _instance;
        }

        if (_service != config_.service_id || _instance != config_.instance_id) {
            LOG_INFO << "### Unknown Service/Instance ["
                    << print_service(_service, _instance)
                    << "] is " << (_is_available ? "Available." : "NOT available.")
                    << LOG_CR;
            if (debug > 2) LOG_TRACE << "// [on_availability] done." << LOG_CR;
            return;
        }
        LOG_INFO << "### Hello Service ["
                 << print_service(_service, _instance)
                 << "] is " << (_is_available ? "Available." : "NOT available.")
                 << LOG_CR;

        // notify request thread that service is available
        if (true) {
            if (debug > 5) LOG_TRACE << "// [on_availability] locking mutex_" << LOG_CR;
            std::lock_guard<std::mutex> its_lock(mutex_);
            is_available_ = _is_available;
            if (debug > 1) LOG_TRACE << "// [on_availability] notify is_available_="
                    << std::boolalpha << is_available_ << LOG_CR;
            condition_.notify_one();
        }

        if (_is_available && subscribe_events_) {
            std::set<vsomeip::eventgroup_t> its_groups;
            its_groups.insert(config_.eventgroup_id);
            LOG_DEBUG << "Requesting Event ["
                    << print_service(_service, _instance) << "/" << to_hex(config_.event_id)
                    << "]" << LOG_CR;
            app_->request_event(
                    config_.service_id, config_.instance_id, config_.event_id,
                    its_groups, vsomeip::event_type_e::ET_FIELD,
                    (use_tcp_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE));
            static bool subscribed = false; // FIXME: prevent double register on reconnect
            if (!subscribed) {
                LOG_DEBUG << "Subscribing EventGroup ["
                        << print_service(config_.service_id, config_.instance_id) << "/"
                        << to_hex(config_.eventgroup_id) << " v" << static_cast<int>(config_.major_version)
                        << "]" << LOG_CR;
                    app_->subscribe(config_.service_id, config_.instance_id, config_.eventgroup_id, config_.major_version);
                subscribed = true;
            }
        }

        if (debug > 2) LOG_TRACE << "// [on_availability] done." << LOG_CR;
    }

    std::string print_delta(int interval, std::chrono::duration<double, std::milli> delta) {
        if (max_delta == 0) return "";

        std::stringstream ss;
        double abs_delta = std::fabs(delta.count() - interval);
        // allow more delta for 1ms, it needs ~0.5ms more
        // if ((interval > 1 && abs_delta > interval * 0.25) || (interval == 1 & abs_delta > interval * 0.75))

        if (abs_delta >= max_delta || abs_delta >= 50) {
            ss << " // Delta: ";
            if (abs_delta <= 1.0) {
                ss << COL_NONE;
            } else if (abs_delta <= 5.0) {
                ss << COL_WHITE;
            } else if (abs_delta <= 10.0) {
                ss << COL_YELLOW;
            } else {
                ss << COL_RED;
            }
            if ((delta.count() - interval) > 0) ss << "+";
            ss << std::fixed << std::setprecision(4) << std::setw(4)
                << (delta.count() - interval) << " ms." << COL_NONE;
        }
        return ss.str();
    }

    void on_subscription_status_changed(const vsomeip::service_t _service,
                                        const vsomeip::instance_t _instance,
                                        const vsomeip::eventgroup_t _eventgroup,
                                        const vsomeip::event_t _event,
                                        const uint16_t error_code) {
        std::stringstream ss;
        ss << "[SOME/IP] Subscription Status: "
           << (error_code ? "Error: " + to_hex(error_code) : "OK")
           << " for Client "
           << print_service(_service, _instance)
           << " group/event " << to_hex(_eventgroup) << "/" << to_hex(_event);
        if (error_code) {
            LOG_ERROR << ss.str() << LOG_CR;
        } else {
            LOG_INFO << ss.str() << LOG_CR;
        }
    }

    void on_hello_event(const std::shared_ptr<vsomeip::message>& _response) {
        HelloEvent event;
        if ((_response->get_return_code() == vsomeip::return_code_e::E_OK) &&
            deserialize_hello_event(event, _response->get_payload())) {
            event_counters_[event.timer_id]++;
            std::string delta_str;
            if (true) {
                auto old_ts = last_event_[event.timer_id];
                auto new_ts = to_time_point(event);
                last_event_[event.timer_id] = new_ts;
                std::chrono::duration<double, std::milli> delta = new_ts - old_ts;
                if (!quiet && old_ts.time_since_epoch().count() > 0) {
                    delta_str = print_delta(timer_interval_ms(event.timer_id), delta);
                }
            }
            if (!quiet) LOG_INFO << "### " << to_string(event) << delta_str << LOG_CR; // COL_NONE << "\r";
        } else {
            LOG_ERROR << "Failed to parse HelloEvent!" << LOG_CR;
        }
    }

    void on_hello_reply(const std::shared_ptr<vsomeip::message>& _response) {
        std::lock_guard<std::mutex> its_lock(request_mutex_);
        HelloResponse response;
        if (debug > 1) {
            LOG_DEBUG << "[on_hello_reply] ### { "
                    << "RC:" << to_string(_response->get_return_code())
                    << ", req_id:" << to_hex(_response->get_request())
                    << ", 0x[ " << bytes_to_string(_response->get_payload()->get_data(), _response->get_payload()->get_length())
                    << "] }" << LOG_CR;
        }
        if (_response->get_return_code() == vsomeip::return_code_e::E_OK) {
            if (deserialize_hello_response(response, _response->get_payload())) {
                if (debug > 0) LOG_DEBUG << "### HelloService response: '" << to_string(response) << "'" << LOG_CR;
            } else {
                LOG_ERROR << "Failed to deserialize HelloResponse payload: ["
                        << bytes_to_string(
                                _response->get_payload()->get_data(),
                                _response->get_payload()->get_length())
                        << "]" << LOG_CR;
            }
        }
        if (hello_req_id_ == _response->get_request()) {
            if (debug > 3) LOG_TRACE << "// HelloService req_id: " << to_hex(hello_req_id_) << " found." << LOG_CR;
            hello_resp_ = response;
            hello_req_id_ = 0;
            request_condition_.notify_one();
        } else {
            LOG_ERROR << "Unexpected HelloService response req_id:" << _response->get_request() << LOG_CR;
        }
    }

    void on_message(const std::shared_ptr<vsomeip::message>& _response) {
        std::stringstream its_message;

        its_message << "[on_message] Received a "
                << to_string(_response->get_message_type()) << " from Service ["
                << to_hex(_response->get_service()) << "."
                << to_hex(_response->get_instance()) << "."
                << to_hex(_response->get_method()) << "] to Client/Session ["
                << to_hex(_response->get_client()) << "/"
                << to_hex(_response->get_session())
                << "] = ";
        std::shared_ptr<vsomeip::payload> its_payload = _response->get_payload();
        its_message << "(" << std::dec << its_payload->get_length() << ") ";

        if (debug > 0) {
            if (debug > 1) {
                its_message << bytes_to_string(its_payload->get_data(), its_payload->get_length());
            }
            LOG_DEBUG << its_message.str() << LOG_CR;
        }
        if (_response->get_return_code() != vsomeip::return_code_e::E_OK) {
            LOG_ERROR << "[on_message] SOME/IP Error: " << to_string(_response->get_return_code()) << LOG_CR;
        }
        if (_response->get_service() == config_.service_id && _response->get_instance() == config_.instance_id &&
            _response->get_method() == config_.event_id)
        {
            on_hello_event(_response);
        } else
        if (_response->get_service() == config_.service_id && _response->get_instance() == config_.instance_id &&
            _response->get_method() == config_.method_id)
        {
            on_hello_reply(_response);

            if (!subscribe_events_ && request_count_ == 0) {
    			LOG_INFO << "### Stopping app (no events)." << LOG_CR;
			    stop();
		    }
        } else {
            LOG_ERROR << "### Got message from unknown Service ["
                << print_service(_response->get_service(), _response->get_instance())
                << "." << to_hex(_response->get_method()) << "]" << LOG_CR;
        }
    }

    void run() {
        // request/response thread
        if (request_count_ == 0) {
            if (debug > 1) LOG_TRACE << "TH: // done. Requests disabled" << LOG_CR;
            return;
        }

        if (debug > 1) LOG_TRACE << "// TH: waiting for init ("
                "is_available=" << std::boolalpha << is_available_
                << ", initialized=" << std::boolalpha << initialized_ << ")" << LOG_CR;
        std::unique_lock<std::mutex> its_lock(mutex_);
        while (running_ && (!initialized_ || !is_available_)) {
            if (debug > 5) LOG_TRACE << "// TH: waiting is_available="
                    << std::boolalpha << is_available_
                    << ", initialized=" << std::boolalpha << initialized_ << LOG_CR;
            condition_.wait(its_lock);
        }
        if (debug > 1) LOG_TRACE << "// TH: init done. is_available=" << std::boolalpha << is_available_ << LOG_CR;

        if (request_count_ > 1) {
            LOG_INFO << LOG_CR;
            LOG_INFO << "### Sending " << request_count_ << " Hello Requests..." << LOG_CR;
            LOG_INFO << LOG_CR;
        }

        HelloRequest req = hello_req_;
        ts_req_start_ = std::chrono::high_resolution_clock::now();
        int hello_sent = 1;
        while (running_) {
            // TODO: wait again if service unavailable?
            if (debug > 0) LOG_DEBUG << "TH: Sending Hello Request ["
                    << hello_sent << "/" << request_count_ << "] "
                    << to_string(req) << " ..." << LOG_CR;

            // Append request# after hello string in muli request case
            if (request_count_ > 1) {
                req.message = hello_req_.message + "#" + std::to_string(hello_sent);
            }
            HelloResponse resp = send_hello(req, true);
            if (hello_sent++ >= request_count_) {
                if (debug > 0) LOG_DEBUG << "TH: Sending finished." << LOG_CR;
                break;
            }
            if (running_ && delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }

        }
        ts_req_finish_ = std::chrono::high_resolution_clock::now();
        requests_sent_ = hello_sent - 1;

        // print_request_summary();

        if (running_ && !subscribe_events_) {
            running_ = false;
            LOG_INFO << "All requests have been sent!" << LOG_CR;
            // prevent this thread from joining in stop()
            request_thread_.detach();
            stop();
        }
        if (debug > 1) LOG_TRACE << "TH: // done." << LOG_CR;
    }

    HelloResponse send_hello(const HelloRequest& hello_reqest, bool wait_response=false) {
        std::unique_lock<std::mutex> its_lock(request_mutex_);

        HelloResponse response = {};

        // Create a new request
        std::shared_ptr<vsomeip::message> rq = vsomeip::runtime::get()->create_request(use_tcp_);
        // Set the VSS service as target of the request
        rq->set_service(config_.service_id);
        rq->set_instance(config_.instance_id);
        rq->set_method(config_.method_id);
        // very important if using != vsomeip::DEFAULT_MAJOR
        if (config_.major_version != vsomeip::ANY_MAJOR) {
             // NOTE: setting ANY_MAJOR is rejected on wire with E_WRONG_INTERFACE_VERSION
            rq->set_interface_version(config_.major_version);
        }

        std::shared_ptr<vsomeip::payload> pl = vsomeip::runtime::get()->create_payload();
        // CreatePayload for Hello Requestfrequest_
        if (!serialize_hello_request(hello_reqest, pl)) {
            LOG_ERROR << "[send_hello] Failed serializing event data: " << to_string(hello_reqest) << LOG_CR;
            rq = nullptr;
            return response;
        }
        rq->set_payload(pl);

        // Send the request to the service. Response will be delivered to the
        // registered message handler
        if (debug > 2) LOG_TRACE
                << "// sending Hello Request to Service["
                << print_service(rq->get_service(), rq->get_instance())
                << "." << to_hex(rq->get_method()) << "]" << LOG_CR;
        app_->send(rq);
        // Save the request id for matching the response (updated on send())
        vsomeip::request_t req_id = rq->get_request();

        hello_req_id_ = req_id;
        if (debug > 0) LOG_INFO << "### Sent HelloRequest: '" << to_string(hello_reqest) << "' req_id:0x" << to_hex(req_id) << LOG_CR; // ", req_id:" << to_hex(req_id) << LOG_CR;
        if (debug > 2) LOG_TRACE
                << "// app_send() -> Service["
                << print_service(rq->get_service(), rq->get_instance())
                << "." << to_hex(rq->get_method()) << "]" << LOG_CR;

        if (running_ && wait_response) {
            if (debug > 2) LOG_TRACE << "[send_hello] // waiting for reply..." << LOG_CR;
            if (request_condition_.wait_for(its_lock, std::chrono::seconds(5)) == std::cv_status::timeout) {
                LOG_ERROR << "### HelloService response timeout: [" << to_string(hello_reqest) << "], req_id:0x" << to_hex(req_id) << LOG_CR;
                return {}; // empty response on timeout
            }
            if (!running_) {
                return {};
            }
            response = hello_resp_;
            if (debug > 2) LOG_TRACE << "[send_hello] // got reply: [" << to_string(response) << "], req_id:0x" << to_hex(req_id) << LOG_CR;

            // sanity checks for reply:
            std::string expected_reply = "Hello " + hello_reqest.message;
            if (response.reply != expected_reply) {
                LOG_ERROR << "### HelloService response mismatch: '"
                        << to_string(response) << "' != '"
                        << to_string(hello_reqest) << "'" << LOG_CR;
            }
        }
        return response;
    }

};

void init_from_environment(client_config_t &config) {
    config.service_id = get_env_uint32("UP_SERVICE", HELLO_SERVICE_ID);
    config.instance_id = get_env_uint32("UP_INSTANCE", HELLO_INSTANCE_ID);
    config.major_version = static_cast<vsomeip::major_version_t>(get_env_uint32("UP_SERVICE_MAJOR", HELLO_SERVICE_MAJOR));
    config.minor_version = static_cast<vsomeip::minor_version_t>(get_env_uint32("UP_SERVICE_MINOR", HELLO_SERVICE_MINOR));

    config.method_id = get_env_uint32("UP_METHOD", HELLO_METHOD_ID);

    config.eventgroup_id = get_env_uint32("UP_EVENTGROUP", HELLO_EVENTGROUP_ID);
    config.event_id = get_env_uint32("UP_EVENT", HELLO_EVENT_ID);
    if (debug > 0) {
        LOG_TRACE << "// [init_from_environment] config:{["
                << print_service_ver(config.service_id, config.instance_id,
                                     config.major_version, config.minor_version)
                << "], method=" << to_hex(config.method_id)
                << ", eventgroup=" << to_hex(config.eventgroup_id)
                << ", event=" << to_hex(config.event_id)
                << "}" << LOG_CR;
    }
}

} // namespace HelloExample

static HelloExample::hello_client* hello_client_ptr = NULL;

static void handle_signal(int _signal) {
    if (hello_client_ptr != NULL &&
            (_signal == SIGINT || _signal == SIGTERM))
        hello_client_ptr->stop();
}

void print_help(const char* name) {
    using HelloExample::to_hex;
    std::cout
            << "Usage: " << name << " {OPTIONS} {NAME}\n"
            << "\n"
            << "NAME:\n"
            << "            If set, Calls HelloService::SayHello(NAME)\n"
            << "\n"
            << "OPTIONS:\n"
            << "  --tcp     Use reliable Some/IP endpoints\n"
            << "  --udp     Use unreliable Some/IP endpoints. Default:true\n"
            << "\n"
            << "  --sub     Subscribe for HelloService events\n"
            << "  --req N   Sends Hello request N times\n"
            << "  --inst ID Use specified instance_id for hello service.\n"
            << "\nENVIRONMENT:\n"
            << "\n  DEBUG           Controls App verbosity (0=info, 1=debug, 2=trace). Default: 1"
            << "\n  QUIET           1=mute all debug/info messages. Default: 0"
            << "\n  DELTA           (benchmark) max delta (ms) from previous timer event. If exceeded dumps Delta warning. Default: 0"
            << "\n  DELAY           ms to wait after sending a SayHello() request (Do not set if benchmarking). Default: 0"
            << "\n"
            // UP HelloService override properties
            << "\n  UP_SERVICE          Use specified u16 value for HelloService service_id.    Default 0x" << to_hex(HELLO_SERVICE_ID) << "\t[-1=ANY]"
            << "\n  UP_INSTANCE         Use specified u16 value for HelloService instance_id.   Default 0x" << to_hex(HELLO_INSTANCE_ID) << "\t[-1=ANY]"
            << "\n  UP_SERVICE_MAJOR    Use specified  u8 value for HelloService major version. Default " << HELLO_SERVICE_MAJOR << "   \t[-1=ANY]"
            << "\n  UP_SERVICE_MINOR    Use specified u32 value for HelloService minor version. Default " << HELLO_SERVICE_MINOR << "   \t[-1=ANY]"
            << "\n  UP_METHOD           Use specified u16 value for HelloService method_id.     Default 0x" << to_hex(HELLO_METHOD_ID)
            << "\n  UP_EVENTGROUP       Use specified u16 value for HelloService eventgroup_id. Default 0x" << to_hex(HELLO_EVENTGROUP_ID)
            << "\n  UP_EVENT            Use specified u16 value for HelloService event_id.      Default 0x" << to_hex(HELLO_EVENT_ID)
            << "\n"
            << std::endl;
}

int main(int argc, char **argv) {

    // default values
    bool use_tcp = false;
    int request_count = 0;
    bool subscribe_events = false;
    int instance_id = HELLO_INSTANCE_ID;

    if (quiet == 1) {
        // make sure all debugs are suppressed
        debug = 0;
    }

    std::string hello_arg;
    std::string arg_tcp_enable("--tcp");
    std::string arg_udp_enable("--udp");
    std::string arg_req("--req");
    std::string arg_inst("--inst");
    std::string arg_subscribe("--sub");

    int i = 1;
    while (i < argc) {
        std::string arg = argv[i];
        // check for options
        if (arg.find("--") == 0) {
            if (arg_tcp_enable == arg) {
                use_tcp = true;
            } else if (arg_udp_enable == arg) {
                use_tcp = false;
            } else if (arg_subscribe == arg) {
                subscribe_events = true;
            } else if (arg_req == arg && i < argc - 1) {
                request_count = std::atoi(argv[++i]);
            } else if (arg_inst == arg && i < argc - 1) {
                instance_id = std::atoi(argv[++i]);
            } else {
                print_help(argv[0]);
                exit(1);
            }
        } else {
            if (!arg.empty()) {
                hello_arg = arg;
            }
        }
        i++;
    }

    if (request_count == 0 && !hello_arg.empty()) {
        request_count = 1;
    }
    // TODO: some sanity checks?
    using HelloExample::DEFAULT_CONFIG;
    init_from_environment(DEFAULT_CONFIG);
    if (instance_id != HELLO_INSTANCE_ID) {
        LOG_TRACE << "// [main] Using instance_id=" << HelloExample::to_hex(instance_id) << LOG_CR;
        DEFAULT_CONFIG.instance_id = instance_id;
    }
    // sanity checks for VSOMEIP environment
    const char* app_name = ::getenv("VSOMEIP_APPLICATION_NAME");
    if (!app_name) {
        LOG_ERROR << "Environment variable VSOMEIP_APPLICATION_NAME not set!" << LOG_CR;
        return 1;
    }
    const char* app_config = ::getenv("VSOMEIP_CONFIGURATION");
    if (!app_config) {
        LOG_ERROR << "Environment variable VSOMEIP_CONFIGURATION not set!" << LOG_CR;
        return 1;
    }

    HelloExample::HelloRequest req = { hello_arg };
    if (debug > 1 && request_count) {
        LOG_TRACE << "// [main] Sending request: [" << to_string(req) << "], count:" << request_count << LOG_CR;
    }
    HelloExample::hello_client client(DEFAULT_CONFIG, use_tcp, subscribe_events, req, request_count);
    hello_client_ptr = &client;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (!client.init()) {
        exit(1);
    }
    client.start();
}
