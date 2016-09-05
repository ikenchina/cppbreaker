#pragma once

#include <string>
#include <chrono>
#include <functional>
#include <mutex>

namespace cppbreaker
{
    class Counts
    {
    public:
        uint32_t requests = 0;
        uint32_t total_successes = 0;
        uint32_t total_failures = 0;
        uint32_t consecutive_successes = 0;
        uint32_t consecutive_failures = 0;

        void onRequest() {
            requests++;
        }
        void onSuccess() {
            total_successes++;
            consecutive_successes++;
            consecutive_failures = 0;
        }
        void onFailure() {
            total_failures++;
            consecutive_failures++;
            consecutive_successes = 0;
        }
        void clear() {
            requests = 0;
            total_successes = 0;
            total_failures = 0;
            consecutive_successes = 0;
            consecutive_failures = 0;
        }

        bool operator==(const Counts& cc) const
        {
            return requests == cc.requests && total_successes == cc.total_successes &&
            total_failures == cc.total_failures && consecutive_successes == cc.consecutive_successes &&
            consecutive_failures == cc.consecutive_failures ;
        }
    };

    enum State
    {
        STATE_CLOSED = 0,
        STATE_HALF_OPEN = 1,
        STATE_OPEN = 2
    };

    struct Settings
    {
        std::string name;

        // max_requests is the maximum number of requests allowed to pass through
        // when the CircuitBreaker is half-open.
        // If max_requests is 0, the CircuitBreaker allows only 1 request.
        uint32_t max_requests = 1;

        // interval is the cyclic period of the closed state
        // for the CircuitBreaker to clear the internal Counts.
        // If interval is 0, the CircuitBreaker doesn't clear internal Counts during the closed state.
        std::chrono::nanoseconds interval = std::chrono::nanoseconds(0);

        // timeout is the period of the open state,
        // after which the state of the CircuitBreaker becomes half-open.
        // If timeout is 0, the timeout value of the CircuitBreaker is set to 60 seconds.
        std::chrono::nanoseconds timeout = std::chrono::seconds(60);

        // ready_to_trip is called with a copy of Counts whenever a request fails in the closed state.
        // If ready_to_trip returns true, the CircuitBreaker will be placed into the open state.
        // If ready_to_trip is nil, default ready_to_trip is used.
        // Default ready_to_trip returns true when the number of consecutive failures is more than 5.
        std::function<bool(const Counts& counts)> ready_to_trip = nullptr;

        // on_state_change is called whenever the state of the CircuitBreaker changes.
        std::function<void(const std::string& name, State from, State to)> on_state_change =  nullptr;
    };

    enum ResultCode
    {
        ResultCodeOK = 0,
        // ErrTooManyRequests is returned when the CB state is half open and the requests count is over the cb maxRequests
        ResultCodeErrTooManyRequests = -0x80000000,
        // ErrOpenState is returned when the CB state is open
        ResultCodeErrOpenState =  -0x70000000
    };

    class CircuitBreaker
    {
    public:
        CircuitBreaker(const Settings& st);
        virtual ~CircuitBreaker() {}

        // std::get<0>(ret) : get expected result returned by Function_
        // std::get<1>(ret) : get code returned by Function_ or circuit breaker
        //                    -0x80000000 and -0x40000000 are reserved for circuit breaker
        template<typename Result_, typename Function_>
        std::tuple<Result_, int> Execute(Function_ req)
        {
            uint64_t generation = 0;
            auto err = beforeRequest(&generation);
            if (err != ResultCodeOK)
                return std::make_tuple(Result_(), (int)err);

            std::tuple<Result_, int> ret = req();
            afterRequest(generation, std::get<1>(ret) == 0);
            return ret;
        }

        State GetState();
        static std::string StateString(State st);

        std::string GetName()
        {
            return settings_.name;
        }

    protected:
        Settings settings_;

        std::mutex mutex_;
        State state_;
        uint64_t generation_ = 0;
        Counts counts_;
        std::chrono::system_clock::time_point expiry_;

    protected:
        bool defaultReadyToTrip(const Counts& counts)
        {
            return counts.consecutive_failures > 5;
        }

        int beforeRequest(uint64_t* gen);

        void afterRequest(uint64_t before, bool success);

        void onSuccess(State st, std::chrono::system_clock::time_point now);

        void onFailure(State st, std::chrono::system_clock::time_point now);

        uint64_t currentState(std::chrono::system_clock::time_point now, State* st);

        void setState(State st, std::chrono::system_clock::time_point now);

        void toNewGeneration(std::chrono::system_clock::time_point now);
    };
}

