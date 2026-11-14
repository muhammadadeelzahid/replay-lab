#pragma once

/**
 * @file
 * @brief `GstVideoBuffer`, which hands decoded GStreamer frames to Qt without copying.
 */

#include "gst_ptr.h"
#include "video_frame_map_guard.h"

#include <QAbstractVideoBuffer>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <gst/video/video.h>

#include <optional>

namespace abrviz::gst {

/**
 * @brief A decoded GStreamer frame handed to Qt without copying.
 *
 * The buffer keeps the sample (and so the GStreamer buffer) alive, maps it with
 * `VideoFrameMapGuard` when Qt asks for the pixels, and releases it when Qt drops the frame.
 */
class GstVideoBuffer final : public QAbstractVideoBuffer {
public:
    /**
     * @brief Takes over a sample.
     *
     * @param sample Must hold a buffer with caps of a raw video format Qt knows (NV12 here).
     * @param info The video layout parsed from the sample's caps.
     * @param format The Qt frame format matching `info`.
     * @see wrap()
     */
    GstVideoBuffer(GstSamplePtr sample, const GstVideoInfo& info, QVideoFrameFormat format);

    /**
     * @brief Unmaps the buffer if Qt did not, then releases the sample.
     */
    ~GstVideoBuffer() override;

    /**
     * @brief Neither copyable nor movable: Qt owns the buffer through the `QVideoFrame`.
     */
    GstVideoBuffer(const GstVideoBuffer&) = delete;
    GstVideoBuffer& operator=(const GstVideoBuffer&) = delete;
    GstVideoBuffer(GstVideoBuffer&&) = delete;
    GstVideoBuffer& operator=(GstVideoBuffer&&) = delete;

    /**
     * @brief Maps the frame's planes for reading.
     *
     * The mapping is kept until `unmap()` or destruction; mapping again reuses it.
     *
     * @param mode Only `QVideoFrame::ReadOnly` is supported: decoded frames are shared with
     *             GStreamer and never written.
     * @return The plane pointers, strides and sizes, or empty data (no planes) for another mode
     *         or if the buffer cannot be mapped.
     */
    MapData map(QVideoFrame::MapMode mode) override;

    /**
     * @brief Releases the mapping made by `map()`.
     */
    void unmap() override;

    /**
     * @brief The frame format given at construction.
     *
     * @return The Qt format of the frame.
     */
    [[nodiscard]] QVideoFrameFormat format() const override { return format_; }

    /**
     * @brief Wraps the sample's buffer in a `QVideoFrame`.
     *
     * The format is NV12 with BT.709 colours in video range, at the size and (when the caps
     * give one) frame rate of the caps.
     *
     * @param sample The sample to hand over; released if it cannot be wrapped.
     * @return The frame, or an invalid frame if the sample has no caps or buffer or the caps are
     *         not raw NV12.
     */
    [[nodiscard]] static QVideoFrame wrap(GstSamplePtr sample);

private:
    GstSamplePtr sample_;                        ///< the sample the frame shows; owns a reference
    GstVideoInfo info_;                          ///< video info parsed from the sample's caps
    QVideoFrameFormat format_;                   ///< the Qt format given at construction
    std::optional<VideoFrameMapGuard> mapping_;  ///< set between `map()` and `unmap()`
};

} // namespace abrviz::gst
