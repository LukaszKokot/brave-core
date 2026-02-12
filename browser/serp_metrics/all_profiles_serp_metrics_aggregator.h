/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_SERP_METRICS_ALL_PROFILES_SERP_METRICS_AGGREGATOR_H_
#define BRAVE_BROWSER_SERP_METRICS_ALL_PROFILES_SERP_METRICS_AGGREGATOR_H_

#include <cstddef>
#include <memory>
#include <vector>

class PrefService;
class ProfileAttributesStorage;

namespace serp_metrics {

class SerpMetrics;

// Aggregates SERP metrics across all profiles. Instances should not be cached
// as they may become out of sync with the SERP metrics of loaded profiles.
class AllProfilesSerpMetricsAggregator {
 public:
  AllProfilesSerpMetricsAggregator(
      PrefService* local_state,
      ProfileAttributesStorage& profile_attributes_storage);

  AllProfilesSerpMetricsAggregator(const AllProfilesSerpMetricsAggregator&) =
      delete;
  AllProfilesSerpMetricsAggregator& operator=(
      const AllProfilesSerpMetricsAggregator&) = delete;

  virtual ~AllProfilesSerpMetricsAggregator();

  virtual size_t GetBraveSearchCountForYesterday() const;
  virtual size_t GetGoogleSearchCountForYesterday() const;
  virtual size_t GetOtherSearchCountForYesterday() const;
  virtual size_t GetSearchCountForStalePeriod() const;

 private:
  std::vector<std::unique_ptr<SerpMetrics>> profile_attributes_serp_metrics_;
};

}  // namespace serp_metrics

#endif  // BRAVE_BROWSER_SERP_METRICS_ALL_PROFILES_SERP_METRICS_AGGREGATOR_H_
