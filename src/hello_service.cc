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
#include <algorithm>
#include <atomic>
#include <csignal>
#include <chrono>
#include <random>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstring>

#include <vsomeip/vsomeip.hpp>

#include "hello_proto.h"
#include "hello_utils.h"
#include "timer.h"

static int debug = ::getenv("DEBUG") ? ::atoi(::getenv("DEBUG")) : 0;
static bool timer_0ms = ::getenv("NO_TIMERS") ? ::atoi(::getenv("NO_TIMERS")) != 0 : false;
static bool toggle_offer = ::getenv("TOGGLE_OFFER") ? ::atoi(::getenv("TOGGLE_OFFER")) != 0 : false;
static bool toggle_ack = ::getenv("TOGGLE_ACK") ? ::atoi(::getenv("TOGGLE_ACK")) != 0 : false;

static const std::string P_ERROR = COL_GREEN + "[HelloSrv] " + COL_RED;
static const std::string P_INFO  = COL_GREEN + "[HelloSrv] " + COL_WHITE_BOLD;
static const std::string P_DEBUG = COL_GREEN + "[HelloSrv] " + COL_NONE;
static const std::string P_TRACE = COL_YELLOW + "[HelloSrv] " + COL_BLUE;

#define LOG_TRACE  std::cout << P_TRACE
#define LOG_DEBUG  std::cout << P_DEBUG
#define LOG_INFO   std::cout << P_INFO
#define LOG_ERROR  std::cerr << P_ERROR
// terminate log msg (reset colors and flush)
#define LOG_CR     COL_NONE << std::endl

namespace HelloExample {

struct service_config_t {
    vsomeip::service_t service_id;
    vsomeip::instance_t instance_id;
    vsomeip::major_version_t major_version;
    vsomeip::minor_version_t minor_version;
    vsomeip::method_t method_id;
    vsomeip::eventgroup_t eventgroup_id;
    vsomeip::event_t event_id;
    std::set<vsomeip::service_t> used_services;
};

service_config_t DEFAULT_CONFIG = {
    HELLO_SERVICE_ID,
    HELLO_INSTANCE_ID,
    HELLO_SERVICE_MAJOR,
    HELLO_SERVICE_MINOR,
    HELLO_METHOD_ID,
    HELLO_EVENTGROUP_ID,
    HELLO_EVENT_ID,
    { }
};

typedef std::map<TimerID, bool> timer_config;

// HelloService Events enabled by default
timer_config timer_enabled = {
    { Timer_1min, true },
    { Timer_1sec, true },
    { Timer_10ms, false },
    { Timer_1ms,  false },
};

const std::map<std::string, HelloExample::TimerID> TIMER_MAPPING = {
    { "1m",   HelloExample::Timer_1min },
    { "1s",   HelloExample::Timer_1sec },
    { "10ms", HelloExample::Timer_10ms },
    { "1ms",  HelloExample::Timer_1ms },
};

template<typename K, typename V>
std::string map_to_string(const std::map<K, V>& map) {
    std::ostringstream ss;
    ss << "{";
    for (auto it = map.begin(); it != map.end(); ++it) {
        ss << it->first << ":" << it->second;
        if (std::next(it) != map.end()) {
            ss << ", ";
        }
    }
    ss << "}";
    return ss.str();
}

bool parse_timers(const std::string& text, timer_config& result) {
    // Parses "<TimerID>:<bool>,<TimerID>:<bool> ...", where:
    //   - <TimerID> maps to TimerID enum (from TIMER_MAPPING)
    //   - <bool> true if specific timer is enabled.
    std::stringstream ss(text);
    std::string item;
    bool ok = true;
    result.clear();
    while (std::getline(ss, item, ',')) {
        int pos = item.find(':');
        if (item.empty() || pos == std::string::npos) {
            LOG_ERROR << "!!! Invalid timer token:" << item << LOG_CR;
            ok = false;
            continue;
        }
        std::string id = item.substr(0, pos);
        auto iter = TIMER_MAPPING.find(id);
        if (iter == TIMER_MAPPING.end()) {
            LOG_ERROR << "!!! Invalid TimerID: " << id << LOG_CR;
            ok = false;
            continue;
        }
        TimerID timer_id = iter->second;
        std::string boolStr = item.substr(pos + 1);
        bool value = (boolStr == "true" || boolStr == "1") ? true : false;
        result[timer_id] = value;
    }
    return ok;
}

class hello_service {

private:
    std::shared_ptr<vsomeip::application> app_;
    bool is_registered_;
    bool use_tcp_;

    service_config_t config_;

    std::mutex mutex_;
    std::condition_variable condition_;
    bool blocked_;
    bool running_;

    std::mutex notify_mutex_;
    std::condition_variable notify_condition_;
    bool is_offered_;

    // std::mutex payload_mutex_;
    std::map<TimerID, std::shared_ptr<vsomeip::payload>> payload_; // separate payloads per timer

    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_condition_;
    bool shutdown_requested_;

    // blocked_ / is_offered_ must be initialized before starting the threads!
    std::thread offer_thread_;
    std::thread notify_thread_;
    std::thread shutdown_thread_;

    Timer timer_;

    // track subscribers and stop sending events (use mutex)
    std::mutex subscribe_mutex_;
    std::condition_variable subscribe_condition_;
    std::atomic_int32_t subscribe_count_;


public:

    hello_service(bool _use_tcp, service_config_t _config)
        : app_(vsomeip::runtime::get()->create_application())
        , config_(_config)
        , is_registered_(false)
        , use_tcp_(_use_tcp)
        , blocked_(false)
        , running_(true)
        , is_offered_(false)
        , subscribe_count_(0)
        , shutdown_requested_(false)
    {

        shutdown_thread_ = std::thread((std::bind(&hello_service::shutdown_th, this)));
        offer_thread_ = std::thread((std::bind(&hello_service::offer_th, this)));
        notify_thread_ = timer_0ms ?
                std::thread((std::bind(&hello_service::notify0_th, this))) :
                std::thread((std::bind(&hello_service::notify_th, this)));

        pthread_setname_np(shutdown_thread_.native_handle(), "hello_shutdown");
        pthread_setname_np(offer_thread_.native_handle(), "hello_offer");
        pthread_setname_np(notify_thread_.native_handle(), "hello_notify");
    }

    ~hello_service() {
        if (std::this_thread::get_id() == shutdown_thread_.get_id()) {
            if (debug > 1) LOG_DEBUG << "[~hello_service] // detaching shutdown_thread_..." << LOG_CR;
            shutdown_thread_.detach();
        } else if (shutdown_thread_.joinable()) {
            if (debug > 1) LOG_DEBUG << "[~hello_service] // joining shutdown_thread_..." << LOG_CR;
            shutdown_thread_.join();
        }
        payload_.clear();
    }


    bool init() {
        std::lock_guard<std::mutex> its_lock(mutex_);

        if (!app_->init()) {
            LOG_ERROR << "Couldn't initialize application" << LOG_CR;
            return false;
        }
        app_->register_state_handler(
                std::bind(&hello_service::on_state, this, std::placeholders::_1));

        {
            payload_[Timer_1min] = vsomeip::runtime::get()->create_payload();
            payload_[Timer_1sec] = vsomeip::runtime::get()->create_payload();
            payload_[Timer_10ms] = vsomeip::runtime::get()->create_payload();
            payload_[Timer_1ms]  = vsomeip::runtime::get()->create_payload();
        }

        register_handlers();

        blocked_ = true;
        condition_.notify_one();
        return true;
    }

    void register_handlers() {
        LOG_INFO << "[register_handlers] Registering message handlers..." << LOG_CR;

        // ALT service handlers
        for (const auto &service: config_.used_services) {
            if (debug > 1) LOG_DEBUG << "[register_handlers]  -- register_message_handler() for Service ["
                << print_service_ver(service, config_.instance_id, config_.major_version, config_.minor_version)
                << " / method:" << to_hex(config_.method_id) << "]" << LOG_CR;
            app_->register_message_handler(service, config_.instance_id, HELLO_METHOD_ID,
                std::bind(&hello_service::on_message_cb, this, std::placeholders::_1));
        }

        std::set<vsomeip::eventgroup_t> event_groups { config_.eventgroup_id};
        for (const auto &service: config_.used_services) {
            if (debug > 1) {
                LOG_TRACE << "[register_handlers]  -- register_subscription_handler() for Eventgroup ["
                    << print_service(service, config_.instance_id)
                    << "." << to_hex(config_.eventgroup_id) << "]" << LOG_CR;
            }
            // experimental: register hello event subscription handler
            app_->register_subscription_handler(
                service, config_.instance_id, config_.eventgroup_id,
                std::bind(&hello_service::on_subscription_cb, this,
                        std::placeholders::_1, std::placeholders::_2,
                        std::placeholders::_3, std::placeholders::_4,
                        service, config_.instance_id, config_.eventgroup_id)
            );
        }
    }

    void unregister_handlers() {
        // keep in sync with register_handlers()
        for (const auto &service: config_.used_services) {
            LOG_DEBUG << "[unregister_handlers] Unregistering handlers for Service:"
                      << print_service(service, config_.instance_id) << LOG_CR;
            app_->unregister_message_handler(service, config_.instance_id, config_.method_id);
            app_->unregister_subscription_handler(service, config_.instance_id, config_.eventgroup_id);
        }
    }


    void start() {
        app_->start();
    }

    ///////////////////////////////////////////////////////////////
    //////////////////////// vsomeip callbacks ////////////////////
    ///////////////////////////////////////////////////////////////

    void on_state(vsomeip::state_type_e _state) {
        LOG_INFO << "Application '" << app_->get_name() << "' is "
                << (_state == vsomeip::state_type_e::ST_REGISTERED ?
                "registered." : "deregistered.") << LOG_CR;

        is_registered_ = (_state == vsomeip::state_type_e::ST_REGISTERED);
        if (is_registered_) {
            // NOTE: handle registered state in case this application is not the routing vsomeip app on the host

            // we are registered at the runtime and can offer our service
            // offer(); -> this generates a blocking state handler!
        }
    }

     /**
      * @brief Subscription handler, returning true to ACK, false for NACK client subscription request
      * NOTE: it seems current impl always returns _clint:0, and for each NACK'ed subscription you also receive an unsubscribe event.
      * That makes it impossible to properly count the active subscribers for deciding when to stop sending events.
      */
    bool on_subscription_cb(
            vsomeip::client_t _client,
            const vsomeip_sec_client_t* _sec_client,
            const std::string& _env,
            bool _is_subscribed,
            // extra args to identify listener
            vsomeip::service_t _service,
            vsomeip::instance_t _instance,
            vsomeip::eventgroup_t _group
    ) {
        // should we acknowledge the subscription?
        bool sub_ack = true;

        // total sub_count, used for toggling ack/nack
        static uint32_t sub_count = 0;
        if (_is_subscribed) {
            ++sub_count;
        }
        if (toggle_ack && _is_subscribed) { // toggle ack/nack for each new subscription
            sub_ack = (sub_count % 2 == 1);
        }
        LOG_INFO << "### [SOME/IP] Subscription handler for Client:"
            << to_hex(_client) << " Service:[" << print_service(_service, _instance)
            << "/" << to_hex(_group) << "]"
            << (_is_subscribed ? " Subscribed" : " Unsubscribed")
            // << ", subscribers:"  << subscribe_count_
            << " -> " << (sub_ack ? COL_GREEN + "ACK " : COL_RED + "NACK")
            << LOG_CR;

        // TODO: count acknowledged subscibers only (currently impossible)
        {
            std::unique_lock<std::mutex> its_lock(subscribe_mutex_);
            if (_is_subscribed) {
                // if (sub_ack)
                subscribe_count_++;
                subscribe_condition_.notify_all();
            } else {
                subscribe_count_--;
            }
            LOG_DEBUG << "### [SOME/IP] Subscribed clients: " << subscribe_count_ << LOG_CR;
        }
        return sub_ack;
    }

    void on_message_cb(const std::shared_ptr<vsomeip::message> &_request) {
        std::shared_ptr<vsomeip::payload> its_payload = _request->get_payload();
        if (debug > 0) {
            std::stringstream its_message;
            its_message << "### [SOME/IP] Received a "
                    << to_string(_request->get_message_type()) << " for Service ["
                    << to_hex(_request->get_service()) << "."
                    << to_hex(_request->get_instance()) << "."
                    << to_hex(_request->get_method()) << "] to Client/Session ["
                    << to_hex(_request->get_client()) << "/"
                    << to_hex(_request->get_session())
                    << "] = (" << std::dec << its_payload->get_length() << ")";

            if (debug > 1) {
                its_message << " ["
                    << bytes_to_string(its_payload->get_data(), its_payload->get_length()) << "]";
            }

            LOG_DEBUG << LOG_CR;
            LOG_DEBUG << its_message.str() << LOG_CR;
            LOG_DEBUG << LOG_CR;
        }
        // create and send response
        std::shared_ptr<vsomeip::message> its_response = vsomeip::runtime::get()->create_response(_request);

        // - sanity checks for protocol version, etc.
        // - handle expected service/instance/method
        vsomeip::service_t req_service = _request->get_service();
        vsomeip::instance_t req_inst = _request->get_instance();
        if (_request->get_protocol_version() != VSOMEIP_PROTOCOL_VERSION) {
            LOG_ERROR << "[on_message] Invalid request protocol version: " << to_hex(_request->get_protocol_version()) << LOG_CR;
            its_response->set_return_code(vsomeip::return_code_e::E_WRONG_PROTOCOL_VERSION);
        } else if (req_service != config_.service_id && config_.used_services.find(req_service) == config_.used_services.end()) {
            LOG_ERROR << "[on_message] request for Unknown Service ID: " << to_hex(req_service) << LOG_CR;
            its_response->set_return_code(vsomeip::return_code_e::E_UNKNOWN_SERVICE);
        } else if (_request->get_interface_version() != config_.major_version &&
                   _request->get_interface_version() != vsomeip::ANY_MAJOR) {
            // TODO: check if requesting for ANY_MAJOR should be allwoed.. allow it for now
            LOG_ERROR << "[on_message] Invalid request interface version: "
                      << std::hex << static_cast<int>(_request->get_interface_version())
                      << ", expected: " << std::hex << static_cast<int>(config_.major_version) << LOG_CR;
            its_response->set_return_code(vsomeip::return_code_e::E_WRONG_INTERFACE_VERSION);
        } else if (req_inst != config_.instance_id) {
            LOG_ERROR << "[on_message] Unknown Instance for Service:"
                      << print_service(req_service, req_inst) << LOG_CR;
            its_response->set_return_code(vsomeip::return_code_e::E_UNKNOWN); // no E_UNKNOWN_INSTANCE defined
        } else if (req_service == config_.service_id || config_.used_services.find(req_service) != config_.used_services.end()) {
            HelloRequest request;
            std::shared_ptr<vsomeip::payload> resp_payload = vsomeip::runtime::get()->create_payload();
            if (deserialize_hello_request(request, its_payload)) {
                if (debug > 0) LOG_DEBUG << "### [SOME/IP]"
                    << " [" << print_service(req_service, req_inst) << "] received: '"
                    << to_string(request) << "'" << LOG_CR;
                // sayHello should return "Hello " + request.message
                HelloResponse response = { "Hello " + request.message };
                serialize_hello_response(response, resp_payload);
                its_response->set_payload(resp_payload);
                its_response->set_return_code(vsomeip::return_code_e::E_OK);
                if (debug > 0) LOG_DEBUG << "### [SOME/IP] Sending Response [" << to_string(response) << "]" << LOG_CR;
            } else {
                LOG_ERROR << "[on_message] Failed to deserialize request payload!" << LOG_CR;
                its_response->set_return_code(vsomeip::return_code_e::E_MALFORMED_MESSAGE);
            }
        } else if (req_inst != config_.instance_id) {
            LOG_ERROR << "### [SOME/IP] Invalid Instance ID: " << to_hex(req_inst) << LOG_CR;
            its_response->set_return_code(vsomeip::return_code_e::E_NOT_OK);
            return;
        } else if (_request->get_method() != config_.method_id) {
            LOG_ERROR << "### [SOME/IP] Invalid Method ID: " << to_hex(_request->get_method()) << LOG_CR;
            its_response->set_return_code(vsomeip::return_code_e::E_UNKNOWN_METHOD);
            return;
        }
        app_->send(its_response);
        if (debug > 1) LOG_TRACE << "[on_message] done." << LOG_CR;
    }

    void on_availability_cb(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
        if (debug > 0) {
            std::stringstream ss;
            ss << "### [SOME/IP] Service ["
                    << print_service(_service, _instance) << "] is "
                    << (_is_available ? "Available." : "NOT available.");
            LOG_DEBUG << ss.str() << LOG_CR;
        }
    }

    /*
     * Called from signal handler to gracefully shutdown
     */
    void shutdown_request() {
        std::unique_lock<std::mutex> its_lock(shutdown_mutex_);
        if (!shutdown_requested_) {
            shutdown_requested_ = true;
            shutdown_condition_.notify_one();
        } else {
            stop();
        }
    }

    /**
     * Shutdown thread, waiting for shutdown_requested_ and calling stop() to minimize chances for deadlocks
     * if invoked directly from signal handler.
     */
    void shutdown_th() {
        std::unique_lock<std::mutex> its_lock(shutdown_mutex_);
        if (debug > 1) LOG_DEBUG << "[shutdown_th] waiting for shutdown..." << LOG_CR;
        while (!shutdown_requested_) {
            shutdown_condition_.wait(its_lock);
        }
        if (debug > 0) LOG_DEBUG << "[shutdown_th] shutdown requested!" << LOG_CR;
        //std::this_thread::sleep_for(std::chrono::seconds(1));
        stop();
        // // Stop offering the service
        // app_->stop_offer_service(service_id, service_instance_id);
        // // unregister the state handler
        // app_->unregister_state_handler();
        // // unregister the message handler
        // app_->unregister_message_handler(service_id, service_instance_id,
        //         service_method_id);
        // // shutdown the application
        // app_->stop();
    }


    void stop() {
        LOG_DEBUG << "[stop] Stopping Application '" << app_->get_name()
                  << "', running: " << running_ << LOG_CR;
        running_ = false;
        blocked_ = true;
        condition_.notify_all();
        notify_condition_.notify_all();

        app_->unregister_state_handler();
        // unregister configured handlers
        unregister_handlers();
        app_->clear_all_handler();

        stop_offer();

        if (debug > 0) LOG_DEBUG << "[stop] stopping timers..." << LOG_CR;
        timer_.stop_timers();
        if (std::this_thread::get_id() == offer_thread_.get_id()) {
            if (debug > 0) LOG_DEBUG << "[stop] detaching offer_thread..." << LOG_CR;
            offer_thread_.detach();
        } else if (offer_thread_.joinable()) {
            if (debug > 0) LOG_DEBUG << "[stop] joining offer_thread..." << LOG_CR;
            offer_thread_.join();
        }
        if (std::this_thread::get_id() == notify_thread_.get_id()) {
            if (debug > 0) LOG_DEBUG << "[stop] detaching notify_thread..." << LOG_CR;
            notify_thread_.detach();
        } else if (notify_thread_.joinable()) {
            if (debug > 0) LOG_DEBUG << "[stop] joining notify_thread..." << LOG_CR;
            notify_thread_.join();
        }
        if (debug > 0) LOG_DEBUG << "[stop] app->stop()" << LOG_CR;
        app_->stop();
    }

    void offer() {
        std::lock_guard<std::mutex> its_lock(notify_mutex_);
        LOG_INFO << "[offer] Application '" << app_->get_name() << "' offering services" << LOG_CR;
        if (debug > 0) {
            std::stringstream ss;
            ss << "[offer]   - offer_service("
            << print_service_ver(config_.service_id, config_.instance_id, config_.major_version, config_.minor_version)
            << ")";
            LOG_DEBUG << ss.str() << LOG_CR;
        }
        app_->offer_service(config_.service_id, config_.instance_id, config_.major_version, config_.minor_version);
        for (const auto& service: config_.used_services) {
            if (debug > 0) {
                std::stringstream ss;
                ss << "[offer]   - offer_service("
                << print_service_ver(service, config_.instance_id, config_.major_version, config_.minor_version)
                << ")";
                LOG_DEBUG << ss.str() << LOG_CR;
            }
            app_->offer_service(service, config_.instance_id, config_.major_version, config_.minor_version);
        }
        // also offer events
        if (debug > 0) {
            std::stringstream ss;
            ss  << "[offer]   - offer_event: "
                << to_hex(config_.event_id) << "/{" << to_hex(config_.eventgroup_id) << "}"
                << " for Service ["
                << print_service_ver(config_.service_id, config_.instance_id, config_.major_version, config_.minor_version)
                << "]";
            LOG_DEBUG << ss.str() << LOG_CR;
        }
        std::set<vsomeip::eventgroup_t> groups = { config_.eventgroup_id };
        app_->offer_event(
                config_.service_id,
                config_.instance_id,
                config_.event_id,
                groups,
                // vsomeip::event_type_e::ET_FIELD, // NOTE: [PRS_SOMEIP_00179] A field shall be a combination of getter, setter and notification event.
                vsomeip::event_type_e::ET_EVENT,
                std::chrono::milliseconds::zero(),
                false, true, nullptr,
                use_tcp_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE);

        if (debug > 2) LOG_TRACE << "[offer] is_offered=true" << LOG_CR;
        is_offered_ = true;
        notify_condition_.notify_one();
    }

    void stop_offer() {
        LOG_INFO << "[stop_offer] Application '" << app_->get_name() << "' stop offering Service ["
                 << print_service_ver(config_.service_id, config_.instance_id, config_.major_version, config_.minor_version)
                 << "]" << LOG_CR;
        app_->stop_offer_service(config_.service_id, config_.instance_id, config_.major_version, config_.minor_version);
        is_offered_ = false;
    }

    void offer_th() {
        if (debug > 1) LOG_TRACE << "[offer_th] started." << LOG_CR;
        // offer thread, reused for set as well
        std::unique_lock<std::mutex> its_lock(mutex_);
        while (running_ && !blocked_) {
            if (debug > 2) LOG_TRACE << "[offer_th] waiting blocked_ ..." << LOG_CR;
            condition_.wait(its_lock);
        }

        bool is_offer = true;
        if (!toggle_offer) {
            offer();
            if (debug > 1) LOG_TRACE << "[offer_th] done. TOGGLE_OFFER=0" << LOG_CR;
            return;
        }
        while (running_) {
            if (is_offer)
                offer();
            else
                stop_offer();

            for (int i = 0; i < 10 && running_; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            is_offer = !is_offer; // Disabled toggling of event availability each 10sec
            if (debug > 1) LOG_TRACE << "[offer_th] toggled offering to "
                    << std::boolalpha << is_offer  << LOG_CR;
        }
        if (debug > 1) LOG_TRACE << "[offer_th] done." << LOG_CR;
    }

    bool notify_event(HelloEvent& event) {
        if (!is_offered_ || !running_) return true; // not sending events..
        if (subscribe_count_ <= 0) {
            if (debug > 4) LOG_TRACE << "[notify_event] No subscribers, skipping event #" << to_string(event.timer_id) << LOG_CR;
            return true;
        }
        if (debug > 1) {
            LOG_DEBUG << "[notify_event] ### " << to_string(event) << LOG_CR;
        }
        auto payload = payload_[event.timer_id];
        if (!serialize_hello_event(event, payload)) {
            LOG_ERROR << "[notify_event] Failed to serialize event" << LOG_CR;
            return false;
        }
        if (debug > 2) {
            LOG_TRACE << "[notify_event] ### app.notify("
                    << print_service(config_.service_id, config_.instance_id) << "/"
                    << to_hex(config_.event_id) << ") -> "
                    << payload->get_length() << " bytes" << LOG_CR;
        }
        if (debug > 3) {
            LOG_TRACE << "[notify_event] Notify payload: ["
                    << bytes_to_string(payload->get_data(), payload->get_length())
                    << "]" << LOG_CR;
        }
        app_->notify(config_.service_id, config_.instance_id, config_.event_id, payload);
        return true;
    }

    /**
     * @brief Single-threaded event notification, sending Timer_1ms events without any delay.
     */
    void notify0_th() {
        if (debug > 2) LOG_TRACE << "[notify0_th] started." << LOG_CR;

        HelloEvent event_1ms = { {}, Timer_1ms };
        while (running_) {
            // wait for service to be offered
            std::unique_lock<std::mutex> its_lock(notify_mutex_);
            while (!is_offered_ && running_) {
                if (debug > 2) LOG_TRACE << "[notify0_th] waiting for is_offered_ ..." << LOG_CR;
                notify_condition_.wait(its_lock);
            }
            // prevent busy loop when service is not offered
            while (is_offered_ && running_) {
                HelloExample::set_hello_event(event_1ms, std::chrono::high_resolution_clock::now());
                notify_event(event_1ms);
            }
        }
        if (debug > 2) LOG_TRACE << "[notify0_th] finished." << LOG_CR;
    }

    void notify_th() {
        if (debug > 2) LOG_TRACE << "[notify_th] started." << LOG_CR;

        HelloEvent event_1s = { {}, Timer_1sec };
        HelloEvent event_1m = { {}, Timer_1min };
        HelloEvent event_1ms = { {}, Timer_1ms };
        HelloEvent event_10ms = { {}, Timer_10ms };

        if (timer_enabled[Timer_1sec]) {
            timer_.add_timer(
                [this, &event_1s](int id) {
                    HelloExample::set_hello_event(event_1s, std::chrono::high_resolution_clock::now());
                    notify_event(event_1s);
                },
                to_int(Timer_1sec), 1000, true);
            if (debug > 1) LOG_TRACE << "[notify_th] Timer_1sec enabled." << LOG_CR;
        }
        if (timer_enabled[Timer_1min]) {
            timer_.add_timer(
                [this, &event_1m](int id) {
                    HelloExample::set_hello_event(event_1m, std::chrono::high_resolution_clock::now());
                    notify_event(event_1m);
                },
                to_int(Timer_1min), 60*1000, true);
            if (debug > 1) LOG_TRACE << "[notify_th] Timer_1min enabled." << LOG_CR;
        }
        if (timer_enabled[Timer_10ms]) {
            timer_.add_timer(
                [this, &event_10ms](int id) {
                    HelloExample::set_hello_event(event_10ms, std::chrono::high_resolution_clock::now());
                    notify_event(event_10ms);
                },
                to_int(Timer_10ms), 10, true);
            if (debug > 1) LOG_TRACE << "[notify_th] Timer_10ms enabled." << LOG_CR;
        }
        if (timer_enabled[Timer_1ms]) {
            timer_.add_timer(
                [this, &event_1ms](int id) {
                    HelloExample::set_hello_event(event_1ms, std::chrono::high_resolution_clock::now());
                    notify_event(event_1ms);
                },
                to_int(Timer_1ms), 1, true);
            if (debug > 1) LOG_TRACE << "[notify_th] Timer_1ms enabled." << LOG_CR;
        }

        while (running_) {
            std::unique_lock<std::mutex> its_lock(notify_mutex_);
            while (!is_offered_ && running_) {
                if (debug > 2) LOG_TRACE << "[notify_th] waiting for is_offered_ ..." << LOG_CR;
                notify_condition_.wait(its_lock);
            }
            // loop just for keepeng event_* in scope
            while (is_offered_ && running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
        if (debug > 2) LOG_TRACE << "[notify_th] finished." << LOG_CR;
    }

};

void init_from_environment(service_config_t &config) {
    config.service_id = get_env_uint32("UP_SERVICE", HELLO_SERVICE_ID);;
    config.instance_id = get_env_uint32("UP_INSTANCE", HELLO_INSTANCE_ID);
    config.major_version = static_cast<vsomeip::major_version_t>(
                           get_env_uint32("UP_SERVICE_MAJOR", HELLO_SERVICE_MAJOR));
    config.minor_version = static_cast<vsomeip::minor_version_t>(
                           get_env_uint32("UP_SERVICE_MINOR", HELLO_SERVICE_MINOR));
    config.method_id = get_env_uint32("UP_METHOD", HELLO_METHOD_ID);
    config.eventgroup_id = get_env_uint32("UP_EVENTGROUP", HELLO_EVENTGROUP_ID);
    config.event_id = get_env_uint32("UP_EVENT", HELLO_EVENT_ID);

    const char* used_service_list = ::getenv("UP_SERVICES");
    config.used_services.clear();
    if (used_service_list) {
        std::istringstream tokenStream(used_service_list);
        std::string token;
        while (std::getline(tokenStream, token, ',')) {
            try {
                vsomeip::service_t serv_id = static_cast<vsomeip::service_t>(parse_uint32(token));
                config.used_services.insert(serv_id);
            } catch (const std::exception& e) {
                LOG_ERROR << "[init_from_environment] Invalid 'UP_SERVICES': " << used_service_list  << LOG_CR;
            }
        }
    }
    // make sure config.service_id is in used_services
    config.used_services.insert(config.service_id);
    if (debug > 0) {
        std::stringstream ss;
        ss << "[init_from_environment] Service["
            << print_service_ver(config.service_id, config.instance_id, config.major_version, config.minor_version)
            << "] Method:" << to_hex(config.method_id)
            << ", Event:" << to_hex(config.event_id)
            << "/{" << to_hex(config.eventgroup_id) << "}"
            << ", UP Services:[ ";
        for (const auto& service : config.used_services) {
            ss << to_hex(service) << " ";
        }
        ss << "]";
        LOG_DEBUG << ss.str() << LOG_CR;
    }
}

} // namespace HelloExample

static HelloExample::hello_service *its_sample_ptr = nullptr;

static void handle_signal(int _signal) {
    if (its_sample_ptr != nullptr && (_signal == SIGINT || _signal == SIGTERM)) {
        // calling stop() from signal handler may cause deadlocks
        //its_sample_ptr->stop();
        its_sample_ptr->shutdown_request();
    }
}

using HelloExample::to_hex;

void print_help(const char* name) {

    std::cout
            << "Usage: " << name << " {OPTIONS}\n\n"
            << "OPTIONS:\n"
            << "  --tcp           Use reliable Some/IP endpoints. (NOTE: needs setting 'reliable' port in json config)\n"
            << "  --udp           Use unreliable Some/IP endpoints. Default:true\n"
            << "\n"
            << "  --timers <LIST> Enable HelloService events. List: [ID:ENABLED,ID:ENABLED,...], where ID:[1s,1m,10ms,1ms], ENABLED:[0,1]\n"
            << "                  Defaults: 1m:1,1s:1,10ms:0,1ms:0\n"
            << "\n"
            << "ENVIRONMENT:\n"
            << "  TIMERS          Enabled timer list (same as --timers). Default: 1m:1,1s:1,10ms:0,1ms:0\n"
            << "  DEBUG           Controls App verbosity (0=info, 1=debug, 2=trace). Default: 1\n"
            << "  TOGGLE_OFFER    (experimental) If set, toggles service offered state periodically. Default: disabled\n"
            << "  TIMER_CB_US     (experimental) Timer callback maximum delay (microseconds). Default: 0=disabled\n"
            << "  TIMER_DEBUG     (experimental) Timer debug level. Default: 0=disabled\n"
            << "  NO_TIMERS       (experimental) if set, disables timers and sends tmer events without any delay.\n"
            // UP HelloService override properties
            << "\n  UP_SERVICE          Use specified u16 value for HelloService service_id.    Default 0x" << to_hex(HELLO_SERVICE_ID)
            << "\n  UP_INSTANCE         Use specified u16 value for HelloService instance_id.   Default 0x" << to_hex(HELLO_INSTANCE_ID)
            << "\n  UP_SERVICE_MAJOR    Use specified  u8 value for HelloService major version. Default " << HELLO_SERVICE_MAJOR
            << "\n  UP_SERVICE_MINOR    Use specified u32 value for HelloService minor version. Default " << HELLO_SERVICE_MINOR
            << "\n  UP_METHOD           Use specified u16 value for HelloService method_id.     Default 0x" << to_hex(HELLO_METHOD_ID)
            << "\n  UP_EVENTGROUP       Use specified u16 value for HelloService eventgroup_id. Default 0x" << to_hex(HELLO_EVENTGROUP_ID)
            << "\n  UP_EVENT            Use specified u16 value for HelloService event_id.      Default 0x" << to_hex(HELLO_EVENT_ID)
            << "\n  UP_SERVICES         Use specified list of alternative HelloService service_id. e.g. \"0x6000,0x60001\", Default: N/A"
            << "\n"
            << std::endl;
}


int main(int argc, char **argv) {

    bool use_tcp = false;
    int32_t instances = 1; // default instance count to offer

    std::string tcp_enable("--tcp");
    std::string udp_enable("--udp");
    std::string timers_arg("--timers");
    std::string help_arg("--help");

    const char* app_config = ::getenv("VSOMEIP_CONFIGURATION");
    const char* app_name = ::getenv("VSOMEIP_APPLICATION_NAME");
    const char* timer_env = ::getenv("TIMERS");

    if (timer_env) {
        HelloExample::parse_timers(std::string(timer_env), HelloExample::timer_enabled);
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (tcp_enable == arg) {
            use_tcp = true;
        } else if (udp_enable == arg) {
            use_tcp = false;
        } else if (timers_arg == arg) {
            std::string timers;
            if (i + 1 < argc) timers = argv[++i];
            // timer ms to enable [1,10,1000,60000]
            bool ok = HelloExample::parse_timers(std::string(timers), HelloExample::timer_enabled);
            if (!ok) {
                LOG_ERROR << "Invalid timer argument: " << timers << LOG_CR;
                print_help(argv[0]);
                exit(1);
            }
        } else if (help_arg == arg) {
            print_help(argv[0]);
            exit(0);
        } else {
            LOG_ERROR << "Invalid argument: " << arg << LOG_CR;
            print_help(argv[0]);
            exit(1);
        }
    }

    // update 1st service to UP_XXX from environment
    HelloExample::init_from_environment(HelloExample::DEFAULT_CONFIG);

    if (debug > 0) {
        LOG_DEBUG << "[main] Enabled timers: " << HelloExample::map_to_string(HelloExample::timer_enabled) << LOG_CR;
    }
    if (vsomeip::DEFAULT_MAJOR != 0) {
        // custom vsomeip used, won't work with "stock" vsomeip clients
        if (debug > 0) LOG_DEBUG << "# Warning: compiled with vsomeip::DEFAULT_MAJOR="
                << std::dec << static_cast<int>(vsomeip::DEFAULT_MAJOR) << LOG_CR;
    }

    // sanity checks for VSOMEIP environment
    if (!app_name) {
        LOG_ERROR << "Environment variable VSOMEIP_APPLICATION_NAME not set!" << LOG_CR;
        exit(2);
    }
    if (!app_config) {
        LOG_ERROR << "Environment variable VSOMEIP_CONFIGURATION not set!" << LOG_CR;
        //exit(2);
    }

    HelloExample::hello_service its_sample(use_tcp, HelloExample::DEFAULT_CONFIG);
    its_sample_ptr = &its_sample;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (its_sample.init()) {
        its_sample.start();
        return 0;
    } else {
        LOG_ERROR << "[main] app.init() failed." << LOG_CR;
        // its_sample.stop(); // deadlocks due to bug in vsomeip.
        exit(1);
    }
}
