c++ circuit breaker
=========


cppbreaker implements the Circuit Breaker pattern in C++.   


## Usage
------------

**CircuitBreaker**

The struct CircuitBreaker is a state machine to prevent sending requests that are likely to fail. 
```
CircuitBreaker(const Settings& st);
```

**Settings**

You can configure CircuitBreaker by the struct Settings:
```
struct Settings
{
    std::string name;                                                  // optional
    uint32_t max_requests = 1;                                         // optional
    std::chrono::nanoseconds interval = std::chrono::nanoseconds(0);   // optional
    std::chrono::nanoseconds timeout = std::chrono::seconds(60);       // optional

    std::function<bool(const Counts& counts)> ready_to_trip = nullptr;                                 // optional
    std::function<void(const std::string& name, State from, State to)> on_state_change =  nullptr;     // optional
};
```
- max_requests : max_requests is the maximum number of requests allowed to pass through when the CircuitBreaker is half-open. If max_requests is 0, the CircuitBreaker allows only 1 request.
- interval : timeout is the period of the open state, after which the state of the CircuitBreaker becomes half-open. If timeout is 0, the timeout value of the CircuitBreaker is set to 60 seconds.
- timeout : timeout is the period of the open state, after which the state of the CircuitBreaker becomes half-open. If timeout is 0, the timeout value of the CircuitBreaker is set to 60 seconds.
- ready_to_trip : ready_to_trip is called with a copy of Counts whenever a request fails in the closed state. If ready_to_trip returns true, the CircuitBreaker will be placed into the open state. If ready_to_trip is nil, default ready_to_trip is used. Default ready_to_trip returns true when the number of consecutive failures is more than 5.
- on_state_change : on_state_change is called whenever the state of the CircuitBreaker changes.


**Execute**

```
// std::get<0>(ret) : get expected result returned by Function_
// std::get<1>(ret) : get code returned by Function_ or circuit breaker
//                    ResultCode::ResultCodeErrTooManyRequests(-0x80000000) and ResultCode::ResultCodeErrOpenState(-0x70000000) are reserved for circuit breaker
template<typename Result_, typename Function_>
std::tuple<Result_, int> Execute(Function_ req)
```

`Function_`
```
std::tuple<Result_, int> demo()
{
    int result_code = 0;
    Result_ result;
    // do something

    return std::make_tuple(result, result_code);
}
```
- `Result_` : 
- `result_code` : On success, result_code is 0; on error, it is not 0. 


Example
------------

**circuit breaker settings**

custom settings
```
cppbreaker::Settings st;
st.name = "test_cb";
st.max_requests = 3;                     // the maximum number of requests when circuitbreaker is half open state
st.interval = std::chrono::seconds(600);  // reset the counts of requests every 600 seconds
st.timeout = std::chrono::seconds(2);     // the period of the open state

// if the number of requests is greater 10 and 60% requests are failed, change circutbreaker to open state
// default condition : consecutive_failures > 5;
st.ready_to_trip = [](const cppbreaker::Counts& counts)->bool {
    auto num_reqs = counts.requests;
    auto failure_ratio = double(counts.total_failures) / double(num_reqs);
    return num_reqs >= 10 && failure_ratio >= 0.6;
};

st.on_state_change = [](const std::string& name, cppbreaker::State from, cppbreaker::State to) {
    // do something
};
```

**create circuit breaker**

```
cppbreaker::CircuitBreaker cb(st);
```

**execution**

```
auto rets = cb.Execute<double>([]()-> std::tuple<double, int> {
    int result_code = demo_rpc_call();
    return std::make_tuple(0.4, result_code);
});
double result = std::get<0>(rets);   // 0.4
int code = std::get<1>(rets);
if (code == cppbreaker::ResultCode::ResultCodeErrOpenState)
{   // circuit breaker is open
}
```

or

```
std::tuple<Object, int> execution_function()
{
    Object object;
    rpc::Status ret = rpc_client()->GetObject(&object);
    return std::make_tuple(object, ret.Ok() ? 0 : 1);
}

auto rets = cb.Execut<Object>(execution_function);
Object object = std::get<0>(rets);
int ret_code = std::get<1>(rets);
```

