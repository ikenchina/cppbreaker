#include <gtest/gtest.h>
#include <memory>
#include <future>
#include "circuit_breaker.h"

using namespace cppbreaker;

class CbTest : public testing::Test
{
};

struct StateChange
{
    StateChange() { from = STATE_CLOSED; to = STATE_CLOSED; }
    StateChange(const std::string& n, State f, State t) {
        name = n; from = f; to = t;
    }
    std::string name;
    State from;
    State to;

    bool operator==(const StateChange& sc) const {
        return name == sc.name && from == sc.from && to == sc.to;
    }
};

static StateChange stateChange;

class testCircuitBreaker : public CircuitBreaker
{
public:
    testCircuitBreaker(const Settings& st) : CircuitBreaker(st)
    {}
    const Settings& settings() {
        return settings_;
    }
    const Counts& counts() {
        return counts_;
    }
    std::chrono::system_clock::time_point expiry() {
        return expiry_;
    }
    void setExpiry(std::chrono::system_clock::time_point ep)
    {
        expiry_ = ep;
    }

    static std::shared_ptr<testCircuitBreaker> newCustom()
    {
        Settings st;
        st.name = "cb";
        st.max_requests = 3;
        st.interval = std::chrono::seconds(30);
        st.timeout = std::chrono::seconds(90);

        st.ready_to_trip = [](const Counts& counts)->bool {
            auto numReqs = counts.requests;
            auto failureRatio = double(counts.total_failures) / double(numReqs);
            return numReqs >= 3 && failureRatio >= 0.6;
        };

        st.on_state_change = [](const std::string& name, State from, State to) {
            stateChange.name = name;
            stateChange.from = from;
            stateChange.to = to;
        };

        return std::make_shared<testCircuitBreaker>(st);
    }

    int fail()
    {
        auto ret = this->Execute<int>([&]()-> std::tuple<int, int> {
            return std::make_tuple(0, 100);
            });
        int err = std::get<1>(ret);
        if (err == 100)
            return 0;
        return err;
    }

    int succeed()
    {
        auto ret = Execute<int>([&]()-> std::tuple<int, int> {
            return std::make_tuple(0, 0);
        });
        int err = std::get<1>(ret);
        return err;
    }

    std::future<int> succeedLater(std::chrono::nanoseconds delay)
    {
        return std::async(std::launch::async, [=]() {
            auto ret = Execute<int>([&]()-> std::tuple<int, int> {
                std::this_thread::sleep_for(delay);
                return std::make_tuple(0, 0);
                });
            return std::get<1>(ret);
            });
    }
};

static Counts newCounts(uint32_t requests, uint32_t total_successes,
    uint32_t total_failures, uint32_t consecutive_successes, uint32_t consecutive_failures)
{
    Counts cc;
    cc.requests = requests; cc.total_successes = total_successes;
    cc.total_failures = total_failures; cc.consecutive_successes = consecutive_successes;
    cc.consecutive_failures = consecutive_failures;
    return cc;
}

void pseudoSleep(testCircuitBreaker* cb, std::chrono::nanoseconds period)
{
    if (cb->expiry().time_since_epoch().count() != 0)
        cb->setExpiry(cb->expiry() + -1 * period);
}

TEST_F(CbTest, TestStateConstants)
{
    ASSERT_EQ(State(0), STATE_CLOSED);
    ASSERT_EQ(State(1), STATE_HALF_OPEN);
    ASSERT_EQ(State(2), STATE_OPEN);
}

TEST_F(CbTest, TestNewCircuitBreaker)
{
    Settings settings;
    Counts defCounts;
    testCircuitBreaker defaultCB(settings);
    ASSERT_EQ("", defaultCB.GetName());
    ASSERT_EQ(1, defaultCB.settings().max_requests);
    ASSERT_EQ(std::chrono::nanoseconds(0), defaultCB.settings().interval);
    ASSERT_EQ(std::chrono::seconds(60), defaultCB.settings().timeout);
    ASSERT_NE(nullptr, defaultCB.settings().ready_to_trip);
    ASSERT_EQ(nullptr, defaultCB.settings().on_state_change);
    ASSERT_EQ(STATE_CLOSED, defaultCB.GetState());
    ASSERT_EQ(defCounts, defaultCB.counts());
    ASSERT_EQ(std::chrono::system_clock::from_time_t(0), defaultCB.expiry());

    auto customCB = testCircuitBreaker::newCustom();
    ASSERT_EQ("cb", customCB->GetName());
    ASSERT_EQ(3, customCB->settings().max_requests);
    ASSERT_EQ(std::chrono::seconds(30), customCB->settings().interval);
    ASSERT_EQ(std::chrono::seconds(90), customCB->settings().timeout);
    ASSERT_NE(nullptr, customCB->settings().ready_to_trip);
    ASSERT_NE(nullptr, customCB->settings().on_state_change);
    ASSERT_EQ(STATE_CLOSED, customCB->GetState());

    ASSERT_EQ(defCounts, customCB->counts());
    ASSERT_NE(0, customCB->expiry().time_since_epoch().count());
}


TEST_F(CbTest, TestDefaultCircuitBreaker)
{
    Settings settings;
    testCircuitBreaker defaultCB(settings);
    ASSERT_EQ(0, defaultCB.expiry().time_since_epoch().count());

    for (int i = 0; i < 5; i++)
    {
        ASSERT_EQ(0, defaultCB.fail());
    }
    ASSERT_EQ(STATE_CLOSED, defaultCB.GetState());
    ASSERT_EQ(newCounts(5, 0, 5, 0, 5), defaultCB.counts());

    ASSERT_EQ(0, defaultCB.succeed());
    ASSERT_EQ(STATE_CLOSED, defaultCB.GetState());
    ASSERT_EQ(newCounts(6, 1, 5, 1, 0), defaultCB.counts());

    ASSERT_EQ(0, defaultCB.fail());
    ASSERT_EQ(STATE_CLOSED, defaultCB.GetState());
    ASSERT_EQ(newCounts(7, 1, 6, 0, 1), defaultCB.counts());

    // StateClosed to StateOpen
    for (int i = 0; i < 5; i++)
    {
        ASSERT_EQ(0, defaultCB.fail());
    }
    ASSERT_EQ(STATE_OPEN, defaultCB.GetState());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), defaultCB.counts());
    ASSERT_NE(0, defaultCB.expiry().time_since_epoch().count());

    ASSERT_NE(0, defaultCB.succeed());
    ASSERT_NE(0, defaultCB.fail());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), defaultCB.counts());

    pseudoSleep(&defaultCB, std::chrono::seconds(59));
    ASSERT_EQ(STATE_OPEN, defaultCB.GetState());

    // StateOpen to StateHalfOpen
    pseudoSleep(&defaultCB, std::chrono::seconds(1));
    ASSERT_EQ(STATE_HALF_OPEN, defaultCB.GetState());
    ASSERT_EQ(0, defaultCB.expiry().time_since_epoch().count());

    // StateHalfOpen to StateOpen
    ASSERT_EQ(0, defaultCB.fail());
    ASSERT_EQ(STATE_OPEN, defaultCB.GetState());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), defaultCB.counts());
    ASSERT_NE(0, defaultCB.expiry().time_since_epoch().count());

    // StateOpen to StateHalfOpen
    pseudoSleep(&defaultCB, std::chrono::seconds(60));
    ASSERT_EQ(STATE_HALF_OPEN, defaultCB.GetState());
    ASSERT_EQ(0, defaultCB.expiry().time_since_epoch().count());

    // StateHalfOpen to StateClosed
    ASSERT_EQ(0, defaultCB.succeed());
    ASSERT_EQ(STATE_CLOSED, defaultCB.GetState());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), defaultCB.counts());
    ASSERT_EQ(0, defaultCB.expiry().time_since_epoch().count());
}


TEST_F(CbTest, TestCustomCircuitBreaker)
{
    auto customCB = testCircuitBreaker::newCustom();
    ASSERT_EQ("cb", customCB->GetName());

    for (int i = 0; i < 5; i++)
    {
        ASSERT_EQ(0, customCB->succeed());
        ASSERT_EQ(0, customCB->fail());
    }
    ASSERT_EQ(STATE_CLOSED, customCB->GetState());
    ASSERT_EQ(newCounts(10, 5, 5, 0, 1), customCB->counts());

    pseudoSleep(customCB.get(), std::chrono::seconds(29));
    ASSERT_EQ(0, customCB->succeed());
    ASSERT_EQ(STATE_CLOSED, customCB->GetState());
    ASSERT_EQ(newCounts(11, 6, 5, 1, 0), customCB->counts());

    pseudoSleep(customCB.get(), std::chrono::seconds(1));
    ASSERT_EQ(0, customCB->fail());
    ASSERT_EQ(STATE_CLOSED, customCB->GetState());
    ASSERT_EQ(newCounts(1, 0, 1, 0, 1), customCB->counts());

    // StateClosed to StateOpen
    ASSERT_EQ(0, customCB->succeed());
    ASSERT_EQ(0, customCB->fail());
    ASSERT_EQ(STATE_OPEN, customCB->GetState());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), customCB->counts());
    ASSERT_NE(0, customCB->expiry().time_since_epoch().count());
    ASSERT_EQ(StateChange("cb", STATE_CLOSED, STATE_OPEN), stateChange);

    // StateOpen to StateHalfOpen
    pseudoSleep(customCB.get(), std::chrono::seconds(90));
    ASSERT_EQ(STATE_HALF_OPEN, customCB->GetState());
    ASSERT_EQ(0, customCB->expiry().time_since_epoch().count());
    ASSERT_EQ(StateChange("cb", STATE_OPEN, STATE_HALF_OPEN), stateChange);

    ASSERT_EQ(0, customCB->succeed());
    ASSERT_EQ(0, customCB->succeed());
    ASSERT_EQ(STATE_HALF_OPEN, customCB->GetState());
    ASSERT_EQ(newCounts(2, 2, 0, 2, 0), customCB->counts());

    // StateHalfOpen to StateClosed
    auto ch = customCB->succeedLater(std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(newCounts(3, 2, 0, 2, 0), customCB->counts());
    ASSERT_NE(0, customCB->succeed());
    ASSERT_EQ(0, ch.get());
    ASSERT_EQ(STATE_CLOSED, customCB->GetState());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), customCB->counts());
    ASSERT_NE(0, customCB->expiry().time_since_epoch().count());
    ASSERT_EQ(StateChange("cb", STATE_HALF_OPEN, STATE_CLOSED), stateChange);
}

TEST_F(CbTest, TestCircuitBreakerInParallel)
{
    auto customCB = testCircuitBreaker::newCustom();
    pseudoSleep(customCB.get(), std::chrono::seconds(29));
    ASSERT_EQ(0, customCB->succeed());
    auto ch = customCB->succeedLater(std::chrono::milliseconds(1500));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(newCounts(2, 1, 0, 1, 0), customCB->counts());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(STATE_CLOSED, customCB->GetState());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), customCB->counts());
    ASSERT_EQ(0, ch.get());
    ASSERT_EQ(newCounts(0, 0, 0, 0, 0), customCB->counts());
}

TEST_F(CbTest, TestGeneration)
{
    auto customCB = testCircuitBreaker::newCustom();
    int numReqs = 10000;
    auto fn = [&]() {
        for (int i = 0; i < numReqs; i++)
        {
            auto ret = customCB->succeed();
            ASSERT_EQ(0, ret);
        }
    };

    auto cpus = std::thread::hardware_concurrency();
    auto totalReqs = cpus * numReqs;
    std::vector<std::thread> threads;
    for (int i = 0; i < cpus; i++)
    {
        threads.emplace_back(std::move(std::thread(fn)));
    }

    for (auto& t : threads)
    {
        t.join();
    }
    ASSERT_EQ(newCounts(totalReqs, totalReqs, 0, totalReqs, 0), customCB->counts());
}


