

#include "threadPool.hpp"




ThreadPool::ThreadPool(size_t threadCount) : stop(false){
    for(size_t i = 0; i < threadCount; i++){
        workers.emplace_back(
            [this]{
                for(;;){
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty()){
                            return;
                        }
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            }
        );
    }
}


ThreadPool::~ThreadPool(){
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for(auto &worker: workers){
       if(worker.joinable()){
           worker.join();
       }
    }
}
