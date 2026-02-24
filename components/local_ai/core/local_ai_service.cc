// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/local_ai/core/local_ai_service.h"

#include <utility>

#include "base/containers/map_util.h"
#include "base/logging.h"

namespace local_ai {

LocalAIService::PendingRequest::PendingRequest() = default;
LocalAIService::PendingRequest::PendingRequest(
    std::string text,
    GenerateEmbeddingsCallback callback)
    : text(std::move(text)), callback(std::move(callback)) {}
LocalAIService::PendingRequest::~PendingRequest() = default;
LocalAIService::PendingRequest::PendingRequest(PendingRequest&&) = default;
LocalAIService::PendingRequest& LocalAIService::PendingRequest::operator=(
    PendingRequest&&) = default;

LocalAIService::LocalAIService(BackgroundWebUIFactory factory)
    : background_web_ui_factory_(std::move(factory)) {
  DVLOG(3) << "LocalAIService created";
}

LocalAIService::~LocalAIService() {
  CloseBackgroundContents();
}

mojo::PendingRemote<mojom::LocalAIService> LocalAIService::MakeRemote() {
  mojo::PendingRemote<mojom::LocalAIService> remote;
  receivers_.Add(this, remote.InitWithNewPipeAndPassReceiver());
  return remote;
}

void LocalAIService::Bind(
    mojo::PendingReceiver<mojom::LocalAIService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void LocalAIService::RegisterOnDeviceModelWorker(
    mojo::PendingRemote<mojom::OnDeviceModelWorker> worker) {
  if (model_worker_remote_.is_bound()) {
    DVLOG(1) << "Model worker already bound, resetting";
    DrainInFlightRequests();
    model_worker_remote_.reset();
  }
  close_timer_.Stop();
  model_worker_remote_.Bind(std::move(worker));

  model_worker_remote_.set_disconnect_handler(base::BindOnce(
      [](LocalAIService* service) {
        DVLOG(1) << "Model worker remote disconnected";
        service->DrainInFlightRequests();
        std::vector<PendingRequest> requests;
        requests.swap(service->pending_requests_);
        for (auto& request : requests) {
          std::move(request.callback).Run({});
        }
        service->CloseBackgroundContents();
      },
      base::Unretained(this)));

  DVLOG(3) << "RegisterOnDeviceModelWorker: Bound model worker";

  ProcessPendingRequests();
}

void LocalAIService::GenerateEmbeddings(const std::string& text,
                                        GenerateEmbeddingsCallback callback) {
  // Ensure BackgroundContents exists (may have been closed due to idle)
  EnsureBackgroundContents();

  if (!model_worker_remote_.is_bound()) {
    DVLOG(3) << "Model worker not ready yet, queuing request";
    pending_requests_.emplace_back(text, std::move(callback));
    return;
  }

  // Reset idle timer since we have activity
  close_timer_.Stop();
  ForwardRequest(text, std::move(callback));
}

void LocalAIService::OnBackgroundContentsReady() {
  DVLOG(3) << "LocalAIService: Background contents ready";
}

void LocalAIService::OnBackgroundContentsDestroyed() {
  DVLOG(1) << "LocalAIService: Background contents destroyed";
  DrainInFlightRequests();
  std::vector<PendingRequest> requests;
  requests.swap(pending_requests_);
  for (auto& request : requests) {
    std::move(request.callback).Run({});
  }
  background_web_ui_.reset();
  model_worker_remote_.reset();
}

void LocalAIService::Shutdown() {
  DVLOG(3) << "LocalAIService: Shutting down";
  CloseBackgroundContents();
}

void LocalAIService::ProcessPendingRequests() {
  if (!model_worker_remote_.is_bound()) {
    return;
  }

  DVLOG(3) << "Processing " << pending_requests_.size() << " pending requests";

  std::vector<PendingRequest> requests;
  requests.swap(pending_requests_);
  for (auto& request : requests) {
    ForwardRequest(request.text, std::move(request.callback));
  }
  MaybeStartIdleTimer();
}

void LocalAIService::ForwardRequest(const std::string& text,
                                    GenerateEmbeddingsCallback callback) {
  uint64_t id = next_request_id_++;
  in_flight_requests_[id] = std::move(callback);
  model_worker_remote_->GenerateEmbeddings(
      text, base::BindOnce(&LocalAIService::OnRequestComplete,
                           weak_ptr_factory_.GetWeakPtr(), id));
}

void LocalAIService::OnRequestComplete(uint64_t request_id,
                                       const std::vector<double>& result) {
  auto* callback = base::FindOrNull(in_flight_requests_, request_id);
  if (callback) {
    std::move(*callback).Run(result);
    in_flight_requests_.erase(request_id);
  }
  MaybeStartIdleTimer();
}

void LocalAIService::MaybeStartIdleTimer() {
  if (!in_flight_requests_.empty()) {
    return;
  }
  close_timer_.Start(FROM_HERE, kCloseTimeout,
                     base::BindOnce(&LocalAIService::CloseBackgroundContents,
                                    weak_ptr_factory_.GetWeakPtr()));
}

void LocalAIService::DrainInFlightRequests() {
  auto requests = std::move(in_flight_requests_);
  in_flight_requests_.clear();
  for (auto& [id, callback] : requests) {
    std::move(callback).Run({});
  }
}

void LocalAIService::EnsureBackgroundContents() {
  if (background_web_ui_) {
    return;
  }

  DVLOG(3) << "LocalAIService: Creating background contents";

  background_web_ui_ = background_web_ui_factory_.Run(this);

  // Start connection timeout — if the worker doesn't register within
  // kCloseTimeout, close the background contents to avoid leaking.
  close_timer_.Start(FROM_HERE, kCloseTimeout,
                     base::BindOnce(&LocalAIService::CloseBackgroundContents,
                                    weak_ptr_factory_.GetWeakPtr()));
}

void LocalAIService::CloseBackgroundContents() {
  DVLOG(3) << "LocalAIService: Closing background contents "
              "to free memory";

  close_timer_.Stop();
  DrainInFlightRequests();
  model_worker_remote_.reset();
  std::vector<PendingRequest> requests;
  requests.swap(pending_requests_);
  for (auto& request : requests) {
    std::move(request.callback).Run({});
  }
  background_web_ui_.reset();
}

}  // namespace local_ai
