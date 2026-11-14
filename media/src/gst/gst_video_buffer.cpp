#include "gst_video_buffer.h"

namespace abrviz::gst {

GstVideoBuffer::GstVideoBuffer(GstSamplePtr sample, const GstVideoInfo& info, QVideoFrameFormat format)
    : sample_(std::move(sample))
    , info_(info)
    , format_(std::move(format))
{
}

GstVideoBuffer::~GstVideoBuffer() = default;  // unmaps (if Qt did not), then drops the sample

QAbstractVideoBuffer::MapData GstVideoBuffer::map(QVideoFrame::MapMode mode)
{
    MapData data;
    if (mode != QVideoFrame::ReadOnly)
        return data;  // decoded frames are shared with GStreamer; never written
    if (!mapping_)
        mapping_.emplace(&info_, gst_sample_get_buffer(sample_.get()), GST_MAP_READ);
    if (!mapping_->isMapped()) {
        mapping_.reset();
        return data;
    }
    const guint planes = mapping_->planeCount();
    data.planeCount = static_cast<int>(planes);
    for (guint i = 0; i < planes && i < 4; ++i) {
        data.data[i] = mapping_->plane(i);
        data.bytesPerLine[i] = mapping_->stride(i);
        const auto height = GST_VIDEO_FRAME_COMP_HEIGHT(&mapping_->frame(), i);
        data.dataSize[i] = mapping_->stride(i) * height;
    }
    return data;
}

void GstVideoBuffer::unmap()
{
    mapping_.reset();
}

QVideoFrame GstVideoBuffer::wrap(GstSamplePtr sample)
{
    GstCaps* caps = gst_sample_get_caps(sample.get());  // borrowed
    GstVideoInfo info;
    if (!caps || !gst_sample_get_buffer(sample.get()) || !gst_video_info_from_caps(&info, caps)
        || GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_NV12)
        return {};
    QVideoFrameFormat format(QSize(GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info)),
                             QVideoFrameFormat::Format_NV12);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
    format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    if (GST_VIDEO_INFO_FPS_N(&info) > 0 && GST_VIDEO_INFO_FPS_D(&info) > 0)
        format.setStreamFrameRate(static_cast<qreal>(GST_VIDEO_INFO_FPS_N(&info))
                                  / GST_VIDEO_INFO_FPS_D(&info));
    return QVideoFrame(std::make_unique<GstVideoBuffer>(std::move(sample), info, std::move(format)));
}

} // namespace abrviz::gst
