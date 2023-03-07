#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  template <class T, class U, class TItems = std::vector<T>>
  std::vector<U> parallel_map(const TItems& items, const std::function<U(const T&)>& func, uint8_t threadCount = std::thread::hardware_concurrency())
  {
    typedef std::pair<const T*, size_t> item_t;
    typedef std::pair<U, size_t> result_t;

    std::queue<item_t> workItems;    
    for (size_t index = 0u; auto& item : items)
    {
      workItems.push(item_t(&item, index++));
    }

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    std::vector<result_t> results;
    results.reserve(items.size());

    std::mutex workItemMutex, resultMutex;
    for (size_t threadIndex = 0u; threadIndex < threadCount; threadIndex++)
    {
      auto threadFunc = [&] {
        const T* input;
        size_t index;

        while (true)
        {
          {
            std::lock_guard<std::mutex> lock(workItemMutex);
            if (workItems.size() == 0u) break;

            std::tie(input, index) = workItems.front();
            workItems.pop();
          }

          auto output = func(*input);

          {
            std::lock_guard<std::mutex> lock(resultMutex);
            results.push_back(result_t(std::move(output), index));
          }
        }
      };

      threads.push_back(std::thread(threadFunc));
    }

    for (auto& thread : threads)
    {
      thread.join();
    }

    std::vector<U> sortedResults(results.size());
    for (auto& [output, index] : results)
    {
      sortedResults[index] = std::move(output);
    }
    return sortedResults;
  }
}