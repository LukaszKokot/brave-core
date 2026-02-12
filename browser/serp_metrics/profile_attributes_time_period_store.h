/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_SERP_METRICS_PROFILE_ATTRIBUTES_TIME_PERIOD_STORE_H_
#define BRAVE_BROWSER_SERP_METRICS_PROFILE_ATTRIBUTES_TIME_PERIOD_STORE_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/raw_ref.h"
#include "brave/components/time_period_storage/time_period_store.h"

class ProfileAttributesStorage;

namespace base {
class ListValue;
}  // namespace base

// Implementation of TimePeriodStore that uses Profile Attributes for
// storage.
class ProfileAttributesTimePeriodStore : public TimePeriodStore {
 public:
  ProfileAttributesTimePeriodStore(
      const base::FilePath& profile_path,
      ProfileAttributesStorage& profile_attributes_storage,
      const std::string& dict_key);

  ~ProfileAttributesTimePeriodStore() override;

  ProfileAttributesTimePeriodStore(const ProfileAttributesTimePeriodStore&) =
      delete;
  ProfileAttributesTimePeriodStore& operator=(
      const ProfileAttributesTimePeriodStore&) = delete;

  // TimePeriodStore:
  void Save(base::ListValue data) override;
  const base::ListValue* Load() const override;
  void Clear() override;

 private:
  const base::FilePath profile_path_;
  const raw_ref<ProfileAttributesStorage> profile_attributes_storage_;
  const std::string dict_key_;
};

#endif  // BRAVE_BROWSER_SERP_METRICS_PROFILE_ATTRIBUTES_TIME_PERIOD_STORE_H_
