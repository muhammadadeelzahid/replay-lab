#pragma once

#include <gst/gst.h>

namespace abrviz::gst {

/// Owns a pipeline. Destroying or resetting it sets the state to NULL first, which stops and
/// joins GStreamer's streaming threads, and then releases the reference. After that no callback
/// of the pipeline can run, so objects the callbacks use may be destroyed.
class PipelineGuard {
public:
    PipelineGuard() = default;
    /// Takes ownership of `pipeline` (a reference we own, not a floating one).
    explicit PipelineGuard(GstElement* pipeline) noexcept : pipeline_(pipeline) {}
    ~PipelineGuard() { reset(); }

    PipelineGuard(const PipelineGuard&) = delete;
    PipelineGuard& operator=(const PipelineGuard&) = delete;
    PipelineGuard(PipelineGuard&&) = delete;
    PipelineGuard& operator=(PipelineGuard&&) = delete;

    [[nodiscard]] GstElement* get() const noexcept { return pipeline_; }
    [[nodiscard]] GstPipeline* pipeline() const noexcept { return GST_PIPELINE_CAST(pipeline_); }
    explicit operator bool() const noexcept { return pipeline_ != nullptr; }

    /// Stops and releases the current pipeline, then owns `pipeline`.
    void reset(GstElement* pipeline = nullptr) noexcept
    {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
        }
        pipeline_ = pipeline;
    }

private:
    GstElement* pipeline_ = nullptr;
};

} // namespace abrviz::gst
