#include "circuit_breaker.h"
#include <iostream>
#include <thread>


std::tuple<std::string, int> execution_demo()
{
    return std::make_tuple("hello demo", 1);
}

int main(int argc, char* argv[])
{
    // mock rpc call
    auto mock_rpc_call = [=](int i) -> int {
        return i < 3 ? 0 : 1;
    };

    // settings
    cppbreaker::Settings st;
    st.name = "test_cb";
    st.max_requests = 3;                     // the maximum number of requests when circuitbreaker is half open state
    st.interval = std::chrono::seconds(600);  // reset the counts of requests every 600 seconds
    st.timeout = std::chrono::seconds(2);     // the period of the open state

    // if the number of requests is greater 10 and 60% requests are failed, change circutbreaker to open state
    st.ready_to_trip = [](const cppbreaker::Counts& counts)->bool {
        auto num_reqs = counts.requests;
        auto failure_ratio = double(counts.total_failures) / double(num_reqs);
        return num_reqs >= 10 && failure_ratio >= 0.6;
    };

    st.on_state_change = [](const std::string& name, cppbreaker::State from, cppbreaker::State to) {
        std::cout << "circuit breaker(" << name << ") : state change from(" << cppbreaker::CircuitBreaker::StateString(from) << ") to(" << cppbreaker::CircuitBreaker::StateString(to) << ")." << std::endl;
    };

    // create circuit breaker
    cppbreaker::CircuitBreaker cb(st);
    
    // close to open
    for (int i = 0; i < 10; i++)
    {
        auto rets = cb.Execute<double>([=]()-> std::tuple<double, int> {
            int result_code = mock_rpc_call(i);
            return std::make_tuple(0.4, result_code);
        });
        double result = std::get<0>(rets);
        int code = std::get<1>(rets);
        if (code != 0)
            std::cout << "error : " << code << std::endl;
        else 
            std::cout << "ok : " << result << std::endl;
    }

    std::cout << "circuit breaker state : " << cb.StateString(cb.GetState()) << std::endl;

    // open to half open
    // sleep 2 seconds, 
    std::this_thread::sleep_for(std::chrono::seconds(2));

    for (int i = 0; i < 4; i++)
    {
        auto rets = cb.Execute<std::string>([=]()-> std::tuple<std::string, int> {
            int result_code = mock_rpc_call(1);
            return std::make_tuple("hello cpp breaker", result_code);
        });
        auto result = std::get<0>(rets);
        int code = std::get<1>(rets);
        if (code != 0)
            std::cout << "error : " << code << std::endl;
        else 
            std::cout << "ok : " << result << std::endl;
        
        std::cout << "circuit breaker state : " << cb.StateString(cb.GetState()) << std::endl;
    }

    auto rets = cb.Execute<std::string>(execution_demo);
    std::cout << std::get<0>(rets) << "  " << std::get<1>(rets) << std::endl;
}