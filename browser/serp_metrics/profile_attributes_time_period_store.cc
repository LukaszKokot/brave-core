/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/serp_metrics/profile_attributes_time_period_store.h"

#include <utility>

#include "base/check.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"

ProfileAttributesTimePeriodStore::ProfileAttributesTimePeriodStore(
    const base::FilePath& profile_path,
    ProfileAttributesStorage& profile_attributes_storage,
    const std::string& dict_key)
    : profile_path_(profile_path),
      profile_attributes_storage_(profile_attributes_storage),
      dict_key_(dict_key) {}

ProfileAttributesTimePeriodStore::~ProfileAttributesTimePeriodStore() = default;

void ProfileAttributesTimePeriodStore::Save(base::ListValue data) {
  ProfileAttributesEntry* entry =
      profile_attributes_storage_->GetProfileAttributesWithPath(profile_path_);
  if (!entry) {
    return;
  }

  base::DictValue serp_metrics;
  if (const base::DictValue* existing_serp_metrics = entry->GetSerpMetrics()) {
    serp_metrics = existing_serp_metrics->Clone();
  }

  serp_metrics.Set(dict_key_, std::move(data));
  entry->SetSerpMetrics(std::move(serp_metrics));
}

const base::ListValue* ProfileAttributesTimePeriodStore::Load() const {
  ProfileAttributesEntry* entry =
      profile_attributes_storage_->GetProfileAttributesWithPath(profile_path_);
  if (!entry) {
    return nullptr;
  }

  const base::DictValue* serp_metrics = entry->GetSerpMetrics();
  if (!serp_metrics) {
    return nullptr;
  }

  return serp_metrics->FindList(dict_key_);
}

void ProfileAttributesTimePeriodStore::Clear() {
  ProfileAttributesEntry* entry =
      profile_attributes_storage_->GetProfileAttributesWithPath(profile_path_);
  if (!entry) {
    return;
  }

  const base::DictValue* serp_metrics = entry->GetSerpMetrics();
  if (!serp_metrics) {
    return;
  }

  base::DictValue updated_metrics = serp_metrics->Clone();
  updated_metrics.Remove(dict_key_);

  entry->SetSerpMetrics(std::move(updated_metrics));
}
