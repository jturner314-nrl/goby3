#include "goby/middleware/coroner/coroner.h"

goby::middleware::HealthMonitorThread::HealthMonitorThread()
    : SimpleThread<NullConfig>(NullConfig(), 1.0 * boost::units::si::hertz)
{
    // handle goby_coroner request
    this->interprocess().template subscribe<groups::health_request, protobuf::HealthRequest>(
        [this](const protobuf::HealthRequest& request) {
            this->interthread().template publish<groups::health_request>(protobuf::HealthRequest());
            waiting_for_responses_ = true;
            last_health_request_time_ = goby::time::SteadyClock::now();

            std::shared_ptr<protobuf::ThreadHealth> our_response(new protobuf::ThreadHealth);
            this->thread_health(*our_response);
            child_responses_[our_response->uid()] = our_response;
        });

    // handle response from main thread
    this->interthread().template subscribe<groups::health_response>(
        [this](std::shared_ptr<const protobuf::ProcessHealth> response) {
            health_response_ = *response;
        });

    // handle response from child threads
    this->interthread().template subscribe<groups::health_response>(
        [this](std::shared_ptr<const protobuf::ThreadHealth> response) {
            child_responses_[response->uid()] = response;
        });
}

void goby::middleware::HealthMonitorThread::loop()
{
    if (waiting_for_responses_ &&
        goby::time::SteadyClock::now() > last_health_request_time_ + health_request_timeout_)
    {
        goby::middleware::protobuf::HealthState health_state =
            goby::middleware::protobuf::HEALTH__OK;

        // overwrite any child responses we got
        for (auto& thread_health : *health_response_.mutable_main()->mutable_child())
        {
            if (child_responses_.count(thread_health.uid()))
                thread_health = *child_responses_.at(thread_health.uid());

            if (thread_health.state() > health_state)
                health_state = thread_health.state();
        }

        health_response_.mutable_main()->set_state(health_state);

        if (health_response_.IsInitialized())
            this->interprocess().template publish<groups::health_response>(health_response_);

        waiting_for_responses_ = false;
        child_responses_.clear();
    }
}
