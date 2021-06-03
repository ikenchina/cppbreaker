#include "circuit_breaker.h"




namespace cppbreaker
{

    CircuitBreaker::CircuitBreaker(const Settings& st)
    {
        settings_ = st;
        state_ = STATE_CLOSED;
        expiry_ = std::chrono::system_clock::from_time_t(0);

        if (settings_.max_requests == 0)
            settings_.max_requests = 1;

        if (settings_.timeout.count() == 0)
            settings_.timeout = std::chrono::seconds(60);

        if (settings_.ready_to_trip == nullptr)
        {
            settings_.ready_to_trip = std::bind(&CircuitBreaker::defaultReadyToTrip, this, std::placeholders::_1);
        }
        toNewGeneration(std::chrono::system_clock::now());
    }

    State CircuitBreaker::GetState()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        State st;
        currentState(now, &st);
        return st;
    }

    std::string CircuitBreaker::StateString(State st)
    {
        if (st == STATE_CLOSED)
            return "close";
        else if (st == STATE_HALF_OPEN)
            return "half open";
        return "open";
    }

    int CircuitBreaker::beforeRequest(uint64_t* gen)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        State st;
        *gen = currentState(now, &st);
        if (st == STATE_OPEN)
        {
            return ResultCodeErrOpenState;
        }
        else if (st == STATE_HALF_OPEN &&
            counts_.requests >= settings_.max_requests)
        {   // too many requests are in flight while state is half open
            return ResultCodeErrTooManyRequests;
        }

        counts_.onRequest();
        return ResultCodeOK;
    }

    void CircuitBreaker::afterRequest(uint64_t before, bool success)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        State st;
        auto generation = currentState(now, &st);

        if (generation != before)
            return;

        if (success)
            onSuccess(st, now);
        else
            onFailure(st, now);
    }

    void CircuitBreaker::onSuccess(State st, std::chrono::system_clock::time_point now)
    {
        switch (st)
        {
        case STATE_CLOSED:
            counts_.onSuccess();
            break;
        case STATE_HALF_OPEN:
        {
            counts_.onSuccess();
            if (counts_.consecutive_successes >= settings_.max_requests)
            {
                setState(STATE_CLOSED, now);
            }
            break;
        }
        default:
        break;
        }
    }

    void CircuitBreaker::onFailure(State st, std::chrono::system_clock::time_point now)
    {
        switch (st)
        {
        case STATE_CLOSED:
        {
            counts_.onFailure();
            if (settings_.ready_to_trip(counts_))
                setState(STATE_OPEN, now);
            break;
        }
        case STATE_HALF_OPEN:
        {
            setState(STATE_OPEN, now);
            break;
        }
        default:
        break;
        }
    }

    uint64_t CircuitBreaker::currentState(std::chrono::system_clock::time_point now, State* st)
    {
        switch (state_)
        {
        case STATE_CLOSED:
        {
            if (expiry_.time_since_epoch().count() != 0 &&
                expiry_ < now)
            {
                toNewGeneration(now);
            }
            break;
        }
        case STATE_OPEN:
        {
            if (expiry_ < now)
            {
                setState(STATE_HALF_OPEN, now);
            }
            break;
        }
        default:
            break;
        }
        *st = state_;
        return generation_;
    }

    void CircuitBreaker::setState(State st, std::chrono::system_clock::time_point now)
    {
        if (state_ == st)
            return;

        auto prev = state_;
        state_ = st;

        toNewGeneration(now);
        if (settings_.on_state_change != nullptr)
        {
            settings_.on_state_change(settings_.name, prev, st);
        }
    }

    void CircuitBreaker::toNewGeneration(std::chrono::system_clock::time_point now)
    {
        generation_++;
        counts_.clear();

        auto zero = std::chrono::system_clock::from_time_t(0);

        switch (state_)
        {
        case STATE_CLOSED:
        {
            if (settings_.interval.count() == 0)
                expiry_ = zero;
            else
                expiry_ = now + settings_.interval;
            break;
        }
        case STATE_OPEN:
            expiry_ = now + settings_.timeout;
            break;
        default:
            expiry_ = zero;
            break;
        }
    }
}
