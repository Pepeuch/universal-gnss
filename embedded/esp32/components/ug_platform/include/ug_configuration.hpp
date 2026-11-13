// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "ug_settings.hpp"
namespace ug::embedded
{
// Called once before services/tasks. Storage failure leaves defaults in RAM, writes locked.
bool StartConfiguration(Capabilities capabilities, std::string& error);
std::shared_ptr<const Settings> Configuration();
const Capabilities& BoardCapabilities();
bool ConfigurationWritable();
bool PatchConfiguration(std::string_view json, std::string& error);
struct ConfigurationHistory
{
  std::uint64_t generation{}, previous_generation{};
  bool previous_available{};
};
ConfigurationHistory ConfigurationBanks();
// Gateway settings only: does not undo receiver commands or clear its journal.
bool RollbackConfiguration(std::string_view request, std::string& error);
// Journal must be committed before any receiver configuration is dispatched.
// An interrupted/indeterminate transaction remains locked across MCU resets.
bool ReceiverJournalBlocked();
bool BeginReceiverJournal();
bool CompleteReceiverJournal();
}  // namespace ug::embedded
