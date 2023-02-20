#pragma once
#include "pch.h"

namespace ShaderGenerator
{
  template <class T, class U, class TItems = std::vector<T>>
  std::vector<U> parallel_map(const TItems& items, std::function<U(const T&)> func, uint8_t threadCount = std::thread::hardware_concurrency())
  {
    typedef std::pair<const T*, size_t> item_t;
    typedef std::pair<U, size_t> result_t;

    std::queue<item_t> workItems;
    size_t index = 0u;
    for (auto& item : items)
    {
      workItems.push(item_t(&item, index++));
    }

    std::mutex workItemSync, resultSync;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    std::vector<result_t> results;
    results.reserve(items.size());
    for (size_t threadIndex = 0u; threadIndex < threadCount; threadIndex++)
    {
      auto threadFunc = [&workItems, &results, &workItemSync, &resultSync, func]() -> void {
          item_t workItem;
          while (true)
          {
            {
              std::lock_guard<std::mutex> lock(workItemSync);
              if (workItems.size() == 0u) break;

              workItem = workItems.front();
              workItems.pop();
            }

            auto result = func(*workItem.first);

            {
              std::lock_guard<std::mutex> lock(resultSync);
              results.push_back(result_t(std::move(result), workItem.second));
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
    for (auto& result : results)
    {
      sortedResults[result.second] = std::move(result.first);
    }
    return sortedResults;
  }
}