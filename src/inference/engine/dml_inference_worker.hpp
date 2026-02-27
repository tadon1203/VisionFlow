#pragma once

#include <functional>
#include <stop_token>
#include <string_view>
#include <system_error>

#include "capture/pipeline/frame_sequencer.hpp"
#include "inference/engine/inference_result_store.hpp"
#include "inference/platform/dml/dml_image_processor.hpp"

namespace vf {

class OnnxDmlSession;

class DmlInferenceWorker {
  public:
    using FaultHandler = std::function<void(std::string_view reason, std::error_code errorCode)>;

    DmlInferenceWorker(FrameSequencer& frameSequencer, OnnxDmlSession& session,
                       DmlImageProcessor& dmlImageProcessor, InferenceResultStore& resultStore,
                       FaultHandler faultHandler);

    void run(const std::stop_token& stopToken);

  private:
    FrameSequencer& frameSequencer;
    OnnxDmlSession& session;
    DmlImageProcessor& dmlImageProcessor;
    InferenceResultStore& resultStore;
    FaultHandler faultHandler;
};

} // namespace vf
