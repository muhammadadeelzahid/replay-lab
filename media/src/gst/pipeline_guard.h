#pragma once

/**
 * @file
 * @brief `PipelineGuard`, the owner of a GStreamer pipeline that stops it before releasing it.
 */

#include <gst/gst.h>

namespace abrviz::gst {

/**
 * @brief Owns a pipeline and stops it before releasing it.
 *
 * Destroying or resetting the guard sets the state to NULL first, which stops and joins
 * GStreamer's streaming threads, and then releases the reference. After that no callback of the
 * pipeline can run, so objects the callbacks use may be destroyed.
 */
class PipelineGuard {
public:
    /**
     * @brief Creates an empty guard.
     */
    PipelineGuard() = default;

    /**
     * @brief Takes ownership of a pipeline.
     *
     * @param pipeline A reference we own, not a floating one; may be null.
     */
    explicit PipelineGuard(GstElement* pipeline) noexcept : pipeline_(pipeline) {}

    /**
     * @brief Sets the pipeline to NULL (joining its streaming threads) and releases it.
     */
    ~PipelineGuard() { reset(); }

    /**
     * @brief Neither copyable nor movable: exactly one guard stops and releases the pipeline.
     */
    PipelineGuard(const PipelineGuard&) = delete;
    PipelineGuard& operator=(const PipelineGuard&) = delete;
    PipelineGuard(PipelineGuard&&) = delete;
    PipelineGuard& operator=(PipelineGuard&&) = delete;

    /**
     * @brief The pipeline as an element.
     *
     * @return The owned pipeline (still owned by the guard), or null if the guard is empty.
     */
    [[nodiscard]] GstElement* get() const noexcept { return pipeline_; }

    /**
     * @brief The pipeline as a `GstPipeline`.
     *
     * @return The owned pipeline (still owned by the guard), or null if the guard is empty.
     */
    [[nodiscard]] GstPipeline* pipeline() const noexcept { return GST_PIPELINE_CAST(pipeline_); }

    /**
     * @brief Whether the guard owns a pipeline.
     *
     * @return True if a pipeline is set.
     */
    explicit operator bool() const noexcept { return pipeline_ != nullptr; }

    /**
     * @brief Stops and releases the current pipeline, then owns another one.
     *
     * The current pipeline is set to NULL, which stops and joins its streaming threads, before
     * its reference is released.
     *
     * @param pipeline The new pipeline (a reference we own, not a floating one), or null to
     *                 leave the guard empty.
     */
    void reset(GstElement* pipeline = nullptr) noexcept
    {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
        }
        pipeline_ = pipeline;
    }

private:
    GstElement* pipeline_ = nullptr;  ///< the owned pipeline reference; null when empty
};

} // namespace abrviz::gst
