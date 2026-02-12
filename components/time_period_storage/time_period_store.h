/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_TIME_PERIOD_STORAGE_TIME_PERIOD_STORE_H_
#define BRAVE_COMPONENTS_TIME_PERIOD_STORAGE_TIME_PERIOD_STORE_H_

namespace base {
class ListValue;
}  // namespace base

class TimePeriodStore {
 public:
  virtual ~TimePeriodStore() = default;

  virtual void Save(base::ListValue data) = 0;

  virtual const base::ListValue* Load() const = 0;

  virtual void Clear() = 0;
};

#endif  // BRAVE_COMPONENTS_TIME_PERIOD_STORAGE_TIME_PERIOD_STORE_H_
