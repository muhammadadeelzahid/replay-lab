#pragma once

/**
 * @file
 * @brief `VideoFrameMapGuard`, a scoped mapping of a GStreamer video buffer.
 */

#include <gst/video/video.h>

namespace abrviz::gst {

/**
 * @brief Maps a video buffer for the guard's lifetime (`gst_video_frame_map` / `_unmap`).
 *
 * The frame keeps a reference to the buffer while it is mapped.
 */
class VideoFrameMapGuard {
public:
    /**
     * @brief Maps a buffer; check `isMapped()` for the result.
     *
     * @param info Layout of the video frames in `buffer`.
     * @param buffer The buffer to map; the mapping holds a reference to it.
     * @param flags Map flags, e.g. `GST_MAP_READ`.
     */
    VideoFrameMapGuard(const GstVideoInfo* info, GstBuffer* buffer, GstMapFlags flags) noexcept
        : mapped_(gst_video_frame_map(&frame_, info, buffer, flags) != FALSE)
    {
    }

    /**
     * @brief Unmaps the frame if it was mapped, releasing the buffer reference.
     */
    ~VideoFrameMapGuard()
    {
        if (mapped_)
            gst_video_frame_unmap(&frame_);
    }

    /**
     * @brief Neither copyable nor movable: the mapping is unmapped exactly once.
     */
    VideoFrameMapGuard(const VideoFrameMapGuard&) = delete;
    VideoFrameMapGuard& operator=(const VideoFrameMapGuard&) = delete;
    VideoFrameMapGuard(VideoFrameMapGuard&&) = delete;
    VideoFrameMapGuard& operator=(VideoFrameMapGuard&&) = delete;

    /**
     * @brief Whether the buffer was mapped.
     *
     * @return False if `gst_video_frame_map` failed; the other accessors are then meaningless.
     */
    [[nodiscard]] bool isMapped() const noexcept { return mapped_; }

    /**
     * @brief The mapped frame.
     *
     * @return The `GstVideoFrame`, valid while the guard lives.
     */
    [[nodiscard]] const GstVideoFrame& frame() const noexcept { return frame_; }


    /**
     * @brief The pixel data of one plane.
     *
     * @param index Plane index, below `planeCount()`.
     * @return Pointer to the first byte of the plane, valid while the guard lives.
     */
    [[nodiscard]] guint8* plane(guint index) const noexcept
    {
        return static_cast<guint8*>(frame_.data[index]);
    }

    /**
     * @brief The stride of one plane.
     *
     * @param index Plane index, below `planeCount()`.
     * @return Bytes per row of the plane.
     */
    [[nodiscard]] int stride(guint index) const noexcept { return frame_.info.stride[index]; }

    /**
     * @brief The number of planes in the frame's format.
     *
     * @return The plane count (2 for NV12).
     */
    [[nodiscard]] guint planeCount() const noexcept { return frame_.info.finfo->n_planes; }

private:
    GstVideoFrame frame_{};  ///< the mapped frame; valid only if `mapped_`
    bool mapped_;  ///< whether `frame_` holds a mapping that must be undone
};

} // namespace abrviz::gst
