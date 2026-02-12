/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/serp_metrics/serp_metrics_util.h"

#include <memory>

#include "base/files/file_path.h"
#include "brave/browser/serp_metrics/profile_attributes_time_period_store.h"
#include "brave/components/serp_metrics/serp_metrics.h"
#include "brave/components/serp_metrics/serp_metrics_feature.h"
#include "brave/components/time_period_storage/time_period_storage.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"

namespace serp_metrics {

namespace {

constexpr char kBraveSearchEngineDictKey[] = "brave_search_engine";
constexpr char kGoogleSearchEngineDictKey[] = "google_search_engine";
constexpr char kOtherSearchEngineDictKey[] = "other_search_engine";

std::unique_ptr<TimePeriodStorage> CreateProfileAttributesTimePeriodStore(
    const base::FilePath& profile_path,
    ProfileAttributesStorage& profile_attributes_storage,
    const std::string& dict_key) {
  return std::make_unique<TimePeriodStorage>(
      std::make_unique<ProfileAttributesTimePeriodStore>(
          profile_path, profile_attributes_storage, dict_key),
      kSerpMetricsTimePeriodInDays.Get(),
      /*should_offset_dst=*/false);
}

}  // namespace

std::unique_ptr<SerpMetrics> CreateSerpMetrics(
    PrefService* local_state,
    const base::FilePath& profile_path,
    ProfileAttributesStorage& profile_attributes_storage) {
  return std::make_unique<SerpMetrics>(
      local_state,
      CreateProfileAttributesTimePeriodStore(
          profile_path, profile_attributes_storage, kBraveSearchEngineDictKey),
      CreateProfileAttributesTimePeriodStore(
          profile_path, profile_attributes_storage, kGoogleSearchEngineDictKey),
      CreateProfileAttributesTimePeriodStore(
          profile_path, profile_attributes_storage, kOtherSearchEngineDictKey));
}

}  // namespace serp_metrics
