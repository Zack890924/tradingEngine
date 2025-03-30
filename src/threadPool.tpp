
#include "threadPool.hpp"
#include <stdexcept>


template <class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;
    //package the task and the variable
    auto task = std::make_shared< std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queueMutex);


        if(stop)
            throw std::runtime_error("Pool has been stopped, cannot submit new tasks");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}