// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_destination_lifecycle.hpp"
#include <cassert>
using Publication = ug::embedded::DestinationPublication<void (*)(void*)>;
int main()
{
  Publication slot;
  int first = 0, second = 0;
  auto callback = +[](void* p) { ++*static_cast<int*>(p); };
  assert(!slot.Applied(0));
  const auto installed = slot.Publish(callback, &first);
  auto cached = slot.Desired();
  assert(!slot.Applied(installed));
  assert(slot.Acknowledge(cached.revision));
  const auto retired = slot.Publish(nullptr, nullptr);
  // The router can still hold an old callback while retirement is pending.
  cached.callback(cached.context);
  assert(first == 1 && !slot.Applied(retired));
  cached = slot.Desired();
  assert(!cached.callback && !cached.context);
  assert(slot.Acknowledge(cached.revision) && slot.Applied(retired));
  const auto replacement = slot.Publish(callback, &second);
  assert(!slot.Applied(replacement));
  assert(!slot.Acknowledge(replacement + 1));
  assert(!slot.Acknowledge(installed));
  cached = slot.Desired();
  cached.callback(cached.context);
  assert(second == 1 && first == 1);
  assert(slot.Acknowledge(replacement));
  // Coalescing publications acknowledges retirement only after the new snapshot
  // has actually replaced the old one. Tickets never acquire a false early ACK.
  const auto removed = slot.Publish(nullptr, nullptr);
  const auto newest = slot.Publish(callback, &first);
  assert(!slot.Applied(removed) && !slot.Applied(newest));
  cached = slot.Desired();
  assert(slot.Acknowledge(cached.revision));
  assert(slot.Applied(removed) && slot.Applied(newest));
}
