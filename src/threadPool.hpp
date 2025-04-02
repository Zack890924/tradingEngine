#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>




class ThreadPool{
    public:
        explicit ThreadPool(size_t threadCount);
        ~ThreadPool();

        template<class F, class... Args>
        auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

        template<class F, class... Args>
        void enqueue(F&& f, Args&&... args) {
            // Just call submit() internally
            submit(std::forward<F>(f), std::forward<Args>(args)...);
        }
        
        
    private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex queueMutex;
        std::condition_variable condition;
        bool stop;

};


#include "threadPool.tpp"