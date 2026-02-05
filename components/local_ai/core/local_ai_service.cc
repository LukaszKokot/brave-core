// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/local_ai/core/local_ai_service.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "brave/components/local_ai/core/local_models_updater.h"
#include "mojo/public/cpp/base/big_buffer.h"

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

namespace {

mojom::ModelFilesPtr LoadModelFilesFromDisk(
    const base::FilePath& weights_path,
    const base::FilePath& weights_dense1_path,
    const base::FilePath& weights_dense2_path,
    const base::FilePath& tokenizer_path,
    const base::FilePath& config_path) {
  auto weights_opt = base::ReadFileToBytes(weights_path);
  if (!weights_opt) {
    DVLOG(0) << "Failed to read model weights from: " << weights_path;
    return nullptr;
  }

  auto weights_dense1_opt = base::ReadFileToBytes(weights_dense1_path);
  if (!weights_dense1_opt) {
    DVLOG(0) << "Failed to read dense1 weights from: " << weights_dense1_path;
    return nullptr;
  }

  auto weights_dense2_opt = base::ReadFileToBytes(weights_dense2_path);
  if (!weights_dense2_opt) {
    DVLOG(0) << "Failed to read dense2 weights from: " << weights_dense2_path;
    return nullptr;
  }

  auto tokenizer_opt = base::ReadFileToBytes(tokenizer_path);
  if (!tokenizer_opt) {
    DVLOG(0) << "Failed to read tokenizer from: " << tokenizer_path;
    return nullptr;
  }

  auto config_opt = base::ReadFileToBytes(config_path);
  if (!config_opt) {
    DVLOG(0) << "Failed to read config from: " << config_path;
    return nullptr;
  }

  DVLOG(1) << "Loaded weights, size: " << weights_opt->size();
  DVLOG(1) << "Loaded weights_dense1, size: " << weights_dense1_opt->size();
  DVLOG(1) << "Loaded weights_dense2, size: " << weights_dense2_opt->size();
  DVLOG(1) << "Loaded tokenizer, size: " << tokenizer_opt->size();
  DVLOG(1) << "Loaded config, size: " << config_opt->size();

  auto model_files = mojom::ModelFiles::New();
  model_files->weights = mojo_base::BigBuffer(std::move(*weights_opt));
  model_files->weights_dense1 =
      mojo_base::BigBuffer(std::move(*weights_dense1_opt));
  model_files->weights_dense2 =
      mojo_base::BigBuffer(std::move(*weights_dense2_opt));
  model_files->tokenizer = mojo_base::BigBuffer(std::move(*tokenizer_opt));
  model_files->config = mojo_base::BigBuffer(std::move(*config_opt));

  return model_files;
}

}  // namespace

LocalAIService::LocalAIService(BackgroundWebUIFactory factory)
    : background_web_ui_factory_(std::move(factory)) {
  DVLOG(3) << "LocalAIService created";

  // Observe the component updater for model readiness
  LocalModelsUpdaterState::GetInstance()->AddObserver(this);
}

LocalAIService::~LocalAIService() {
  LocalModelsUpdaterState::GetInstance()->RemoveObserver(this);
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
  // Bind the single model worker remote from our WASM WebContents
  if (model_worker_remote_.is_bound()) {
    DVLOG(1) << "Model worker already bound, resetting";
    DrainInFlightRequests();
    model_worker_remote_.reset();
    model_initialized_ = false;
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

  // The WASM page has loaded and registered its worker — mark ready
  wasm_page_loaded_ = true;

  // Try to load model now that remote is bound
  TryLoadModel();
}

void LocalAIService::LoadModelFiles() {
  if (!model_worker_remote_) {
    DVLOG(0) << "Model worker interface not bound";
    OnModelInitialized(false);
    return;
  }

  // Get model file paths from LocalModelsUpdaterState
  base::FilePath weights_path =
      LocalModelsUpdaterState::GetInstance()->GetEmbeddingGemmaModel();
  base::FilePath weights_dense1_path =
      LocalModelsUpdaterState::GetInstance()->GetEmbeddingGemmaDense1();
  base::FilePath weights_dense2_path =
      LocalModelsUpdaterState::GetInstance()->GetEmbeddingGemmaDense2();
  base::FilePath tokenizer_path =
      LocalModelsUpdaterState::GetInstance()->GetEmbeddingGemmaTokenizer();
  base::FilePath config_path =
      LocalModelsUpdaterState::GetInstance()->GetEmbeddingGemmaConfig();

  const base::FilePath& model_dir =
      LocalModelsUpdaterState::GetInstance()->GetEmbeddingGemmaModelDir();

  if (model_dir.empty()) {
    DVLOG(0) << "LocalAIService: Model directory not set "
                "in updater state";
    OnModelInitialized(false);
    return;
  }

  // Store model directory for potential retries
  pending_model_path_ = model_dir;

  DVLOG(1) << "Loading model files (attempt " << (model_load_retry_count_ + 1)
           << "/" << kMaxModelLoadRetries << "):";
  DVLOG(1) << "Weights: " << weights_path;
  DVLOG(1) << "Weights Dense1: " << weights_dense1_path;
  DVLOG(1) << "Weights Dense2: " << weights_dense2_path;
  DVLOG(1) << "Tokenizer: " << tokenizer_path;
  DVLOG(1) << "Config: " << config_path;

  // Load model files on a background thread to avoid blocking
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&LoadModelFilesFromDisk, weights_path, weights_dense1_path,
                     weights_dense2_path, tokenizer_path, config_path),
      base::BindOnce(&LocalAIService::OnModelFilesLoaded,
                     weak_ptr_factory_.GetWeakPtr()));
}

void LocalAIService::OnModelFilesLoaded(mojom::ModelFilesPtr model_files) {
  DVLOG(3) << "LocalAIService::OnModelFilesLoaded called";

  if (!model_files) {
    DVLOG(0) << "Failed to load model files from disk";
    OnModelInitialized(false);
    return;
  }

  DVLOG(3) << "Calling model_worker_remote_->Init()...";
  model_worker_remote_->Init(std::move(model_files),
                             base::BindOnce(&LocalAIService::OnModelInitialized,
                                            weak_ptr_factory_.GetWeakPtr()));
}

void LocalAIService::GenerateEmbeddings(const std::string& text,
                                        GenerateEmbeddingsCallback callback) {
  // Ensure BackgroundContents exists (may have been closed due to idle)
  EnsureBackgroundContents();

  // If model is not initialized yet, queue the request
  if (!model_initialized_) {
    DVLOG(3) << "Model not initialized yet, queuing request";
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

void LocalAIService::OnLocalModelsReady(const base::FilePath& install_dir) {
  DVLOG(3) << "LocalAIService: Local models ready at: " << install_dir;
  models_ready_ = true;

  // Try to load model if both conditions are met
  TryLoadModel();
}

void LocalAIService::TryLoadModel() {
  DVLOG(3) << "LocalAIService::TryLoadModel"
           << " - wasm_page_loaded_=" << wasm_page_loaded_
           << ", models_ready_=" << models_ready_
           << ", remote_bound=" << model_worker_remote_.is_bound()
           << ", model_initialized_=" << model_initialized_;

  if (!wasm_page_loaded_) {
    DVLOG(3) << "LocalAIService: Waiting for WASM page to load...";
    return;
  }

  if (!models_ready_) {
    DVLOG(3) << "LocalAIService: Waiting for models to be ready...";
    return;
  }

  if (!model_worker_remote_.is_bound()) {
    DVLOG(1) << "LocalAIService: WASM page loaded but remote "
                "not bound yet";
    return;
  }

  if (model_initialized_) {
    DVLOG(3) << "LocalAIService: Model already initialized";
    return;
  }

  DVLOG(3) << "LocalAIService: Both WASM and component ready, "
              "loading model...";
  LoadModelFiles();
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
  wasm_page_loaded_ = false;
  model_initialized_ = false;
}

void LocalAIService::Shutdown() {
  DVLOG(3) << "LocalAIService: Shutting down";
  CloseBackgroundContents();
}

void LocalAIService::ProcessPendingRequests() {
  if (!model_initialized_ || !model_worker_remote_) {
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
  auto it = in_flight_requests_.find(request_id);
  if (it != in_flight_requests_.end()) {
    std::move(it->second).Run(result);
    in_flight_requests_.erase(it);
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

  // Reset state so we can reinitialize later
  wasm_page_loaded_ = false;
  model_initialized_ = false;
  model_load_retry_count_ = 0;
}

void LocalAIService::OnModelInitialized(bool success) {
  DVLOG(3) << "LocalAIService::OnModelInitialized called "
              "with success="
           << success;

  if (success) {
    DVLOG(3) << "LocalAIService: Model loaded successfully! "
                "History embeddings are now ready.";
    model_load_retry_count_ = 0;
    model_initialized_ = true;

    DVLOG(3) << "Processing " << pending_requests_.size()
             << " pending requests";
    ProcessPendingRequests();
  } else {
    model_load_retry_count_++;
    model_initialized_ = false;

    if (model_load_retry_count_ < kMaxModelLoadRetries) {
      DVLOG(1) << "LocalAIService: Failed to load model (attempt "
               << model_load_retry_count_ << "/" << kMaxModelLoadRetries
               << "). Retrying in 100ms...";
      RetryLoadModel();
    } else {
      DVLOG(0) << "LocalAIService: Failed to load model after "
               << kMaxModelLoadRetries << " attempts. "
               << "History embeddings will not work. "
               << "Make sure model files are downloaded via "
                  "component updater.";
      model_load_retry_count_ = 0;
    }
  }
}

void LocalAIService::RetryLoadModel() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&LocalAIService::LoadModelFiles,
                     weak_ptr_factory_.GetWeakPtr()),
      base::Milliseconds(100));
}

}  // namespace local_ai
