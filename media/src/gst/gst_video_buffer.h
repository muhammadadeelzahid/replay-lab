#pragma once

#include "gst_ptr.h"
#include "video_frame_map_guard.h"

#include <QAbstractVideoBuffer>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <gst/video/video.h>

#include <optional>

namespace abrviz::gst {

/// A decoded GStreamer frame handed to Qt without copying: the buffer keeps the sample (and so
/// the GStreamer buffer) alive, maps it with `VideoFrameMapGuard` when Qt asks for the pixels,
/// and releases it when Qt drops the frame.
class GstVideoBuffer final : public QAbstractVideoBuffer {
public:
    /// `sample` must hold a buffer with caps of a raw video format Qt knows (NV12 here).
    GstVideoBuffer(GstSamplePtr sample, const GstVideoInfo& info, QVideoFrameFormat format);
    ~GstVideoBuffer() override;

    GstVideoBuffer(const GstVideoBuffer&) = delete;
    GstVideoBuffer& operator=(const GstVideoBuffer&) = delete;
    GstVideoBuffer(GstVideoBuffer&&) = delete;
    GstVideoBuffer& operator=(GstVideoBuffer&&) = delete;

    MapData map(QVideoFrame::MapMode mode) override;
    void unmap() override;
    [[nodiscard]] QVideoFrameFormat format() const override { return format_; }

    /// Wraps the sample's buffer in a `QVideoFrame`, or returns an invalid frame if the caps
    /// are not raw NV12.
    [[nodiscard]] static QVideoFrame wrap(GstSamplePtr sample);

private:
    GstSamplePtr sample_;
    GstVideoInfo info_;
    QVideoFrameFormat format_;
    std::optional<VideoFrameMapGuard> mapping_;
};

} // namespace abrviz::gst
