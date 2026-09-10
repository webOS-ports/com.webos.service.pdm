// Copyright (c) 2019-2021 LG Electronics, Inc.
// Copyright (c) 2012 Jakob Progsch, Václav Zeman
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software
// in a product, an acknowledgment in the product documentation would be
// appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
// distribution.

#ifndef PDM_THREAD_POOL_H
#define PDM_THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include "PdmLogUtils.h"

class PdmThreadPool {

private:
    bool terminate;
    std::vector< std::thread > workers;
    std::condition_variable condition;
    std::mutex queue_mutex;
    std::queue< std::function<void()> > pdm_tasks;

public:
    PdmThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;
    ~PdmThreadPool();

};


inline PdmThreadPool::PdmThreadPool(size_t threads)
    :   terminate(false)
{
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(
            [this]
            {
                for(;;)
                {
                    std::function<void()> task = [] () {};

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->terminate || !this->pdm_tasks.empty(); });
                        if(this->terminate && this->pdm_tasks.empty())
                            return;
                        task = std::move(this->pdm_tasks.front());
                        this->pdm_tasks.pop();
                    }

                    task();
                }
            }
        );
}


template<class F, class... Args>
auto PdmThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    /* std::result_of, which this used, is deprecated in C++17 and removed in
     * C++20. invoke_result is the replacement and means the same thing here. */
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if(terminate)
            throw std::runtime_error("enqueue on terminated PdmThreadPool");

        pdm_tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

inline PdmThreadPool::~PdmThreadPool()
{
    {
        try {
            std::unique_lock<std::mutex> lock(queue_mutex);
            terminate = true;
        }
        catch(const std::system_error& e) {
            PDM_LOG_ERROR("Exception occured : %s", e.what());
        }
    }
    condition.notify_all();
    for(std::thread &worker: workers) {
        if (worker.joinable()) {
            try {
                worker.join();
            }
            catch (std::exception &e) {
                PDM_LOG_ERROR("Exception occurred : %s", e.what());
            }
        }
    }
}

#endif  //PDM_THREAD_POOL_H
