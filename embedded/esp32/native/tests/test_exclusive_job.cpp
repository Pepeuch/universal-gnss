// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_exclusive_job.hpp"
#include <cassert>
#include <thread>
int main()
{
  using ug::embedded::ExclusiveJob;
  ExclusiveJob job;
  assert(!job.Return() && !job.Reclaim() && !job.Take());
  assert(job.Request() && job.CancelPending() && !job.Take());
  assert(job.Request() && job.Take() && !job.CancelPending());
  assert(job.Return() && job.Reclaim());
  unsigned input{}, output{};
  std::thread helper([&] {
    for (unsigned n = 1; n <= 10000; ++n)
    {
      while (!job.Take())
        std::this_thread::yield();
      assert(input == n);
      output = input * 2;
      assert(job.Return());
    }
  });
  for (unsigned n = 1; n <= 10000; ++n)
  {
    input = n;
    assert(job.Request());
    assert(!job.Request());
    while (!job.Reclaim())
      std::this_thread::yield();
    assert(output == n * 2);
  }
  helper.join();
  assert(job.state() == ExclusiveJob::State::kOwner);
}
