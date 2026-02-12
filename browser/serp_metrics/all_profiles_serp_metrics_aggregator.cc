/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/serp_metrics/all_profiles_serp_metrics_aggregator.h"

#include <numeric>

#include "base/feature_list.h"
#include "brave/browser/serp_metrics/serp_metrics_util.h"
#include "brave/components/serp_metrics/serp_metrics.h"
#include "brave/components/serp_metrics/serp_metrics_feature.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"

namespace serp_metrics {

namespace {

using GetMetricsFunctionType = size_t (SerpMetrics::*)() const;
size_t AggregateMetrics(
    const std::vector<std::unique_ptr<SerpMetrics>>& container,
    GetMetricsFunctionType func) {
  return std::accumulate(container.begin(), container.end(), size_t{0},
                         [func](size_t acc, const auto& serp_metrics) {
                           return acc + (serp_metrics.get()->*func)();
                         });
}

}  // namespace

AllProfilesSerpMetricsAggregator::AllProfilesSerpMetricsAggregator(
    PrefService* local_state,
    ProfileAttributesStorage& profile_attributes_storage) {
  if (!base::FeatureList::IsEnabled(serp_metrics::kSerpMetricsFeature)) {
    return;
  }

  for (ProfileAttributesEntry* entry :
       profile_attributes_storage.GetAllProfilesAttributes()) {
    const base::FilePath& profile_path = entry->GetPath();
    profile_attributes_serp_metrics_.push_back(CreateSerpMetrics(
        local_state, profile_path, profile_attributes_storage));
  }
}

AllProfilesSerpMetricsAggregator::~AllProfilesSerpMetricsAggregator() = default;

size_t AllProfilesSerpMetricsAggregator::GetBraveSearchCountForYesterday()
    const {
  return AggregateMetrics(profile_attributes_serp_metrics_,
                          &SerpMetrics::GetBraveSearchCountForYesterday);
}

size_t AllProfilesSerpMetricsAggregator::GetGoogleSearchCountForYesterday()
    const {
  return AggregateMetrics(profile_attributes_serp_metrics_,
                          &SerpMetrics::GetGoogleSearchCountForYesterday);
}

size_t AllProfilesSerpMetricsAggregator::GetOtherSearchCountForYesterday()
    const {
  return AggregateMetrics(profile_attributes_serp_metrics_,
                          &SerpMetrics::GetOtherSearchCountForYesterday);
}

size_t AllProfilesSerpMetricsAggregator::GetSearchCountForStalePeriod() const {
  return AggregateMetrics(profile_attributes_serp_metrics_,
                          &SerpMetrics::GetSearchCountForStalePeriod);
}

}  // namespace serp_metrics
