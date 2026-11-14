#pragma once

#include <gst/video/video.h>

namespace abrviz::gst {

/// Maps a video buffer for the guard's lifetime (`gst_video_frame_map` / `_unmap`). The frame
/// keeps a reference to the buffer while it is mapped.
class VideoFrameMapGuard {
public:
    VideoFrameMapGuard(const GstVideoInfo* info, GstBuffer* buffer, GstMapFlags flags) noexcept
        : mapped_(gst_video_frame_map(&frame_, info, buffer, flags) != FALSE)
    {
    }
    ~VideoFrameMapGuard()
    {
        if (mapped_)
            gst_video_frame_unmap(&frame_);
    }

    VideoFrameMapGuard(const VideoFrameMapGuard&) = delete;
    VideoFrameMapGuard& operator=(const VideoFrameMapGuard&) = delete;
    VideoFrameMapGuard(VideoFrameMapGuard&&) = delete;
    VideoFrameMapGuard& operator=(VideoFrameMapGuard&&) = delete;

    [[nodiscard]] bool isMapped() const noexcept { return mapped_; }
    [[nodiscard]] const GstVideoFrame& frame() const noexcept { return frame_; }

    [[nodiscard]] guint8* plane(guint index) const noexcept
    {
        return static_cast<guint8*>(frame_.data[index]);
    }
    [[nodiscard]] int stride(guint index) const noexcept { return frame_.info.stride[index]; }
    [[nodiscard]] guint planeCount() const noexcept { return frame_.info.finfo->n_planes; }

private:
    GstVideoFrame frame_{};
    bool mapped_;
};

} // namespace abrviz::gst
