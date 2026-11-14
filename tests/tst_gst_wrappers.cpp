#include "gst/gst_ptr.h"
#include "gst/gst_runtime.h"
#include "gst/pipeline_guard.h"
#include "gst/video_frame_map_guard.h"

#include <QTest>

#include <thread>
#include <type_traits>
#include <vector>

using namespace abrviz::gst;

static_assert(!std::is_copy_constructible_v<PipelineGuard> && !std::is_move_constructible_v<PipelineGuard>);
static_assert(!std::is_copy_constructible_v<VideoFrameMapGuard>
              && !std::is_move_constructible_v<VideoFrameMapGuard>);

namespace {

guint refCount(gpointer object)
{
    return static_cast<guint>(GST_OBJECT_REFCOUNT_VALUE(object));
}

guint miniRefCount(gconstpointer object)
{
    return static_cast<guint>(GST_MINI_OBJECT_REFCOUNT_VALUE(object));
}

} // namespace

class TestGstWrappers : public QObject {
    Q_OBJECT

private slots:
    void initialisesOnceFromManyThreads()
    {
        std::vector<std::jthread> threads;
        std::atomic<int> ok{0};
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&ok] {
                if (ensureInitialized())
                    ok.fetch_add(1);
            });
        threads.clear();  // join
        QCOMPARE(ok.load(), 8);
        QVERIFY(gst_is_initialized());
        QString missing;
        QVERIFY2(hasPipelineElements(&missing), qPrintable(missing));
    }

    void gstPtrReleasesItsReference()
    {
        GstPtr<GstElement> element = makeElement("fakesink");
        QVERIFY(element);
        QVERIFY(!g_object_is_floating(element.get()));
        GstElement* raw = element.get();
        gst_object_ref(raw);  // a second reference to watch the count
        QCOMPARE(refCount(raw), 2u);
        {
            GstPtr<GstElement> moved = std::move(element);
            QVERIFY(!element);
            QCOMPARE(refCount(raw), 2u);  // moving does not touch the count
        }
        QCOMPARE(refCount(raw), 1u);
        gst_object_unref(raw);
        QVERIFY(!makeElement("no-such-element"));
    }

    void miniObjectPointersRelease()
    {
        GstCaps* caps = gst_caps_new_empty_simple("video/x-raw");
        gst_caps_ref(caps);
        { const GstCapsPtr owner(caps); QCOMPARE(miniRefCount(caps), 2u); }
        QCOMPARE(miniRefCount(caps), 1u);

        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
        GstSample* sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        gst_buffer_unref(buffer);
        gst_sample_ref(sample);
        { const GstSamplePtr owner(sample); QCOMPARE(miniRefCount(sample), 2u); }
        QCOMPARE(miniRefCount(sample), 1u);
        gst_sample_unref(sample);
        gst_caps_unref(caps);

        GstMessage* message = gst_message_new_eos(nullptr);
        gst_message_ref(message);
        { const GstMessagePtr owner(message); QCOMPARE(miniRefCount(message), 2u); }
        QCOMPARE(miniRefCount(message), 1u);
        gst_message_unref(message);
    }

    void pipelineGuardStopsThePipeline()
    {
        GstElement* pipeline = gst_parse_launch("videotestsrc is-live=true ! fakesink sync=true", nullptr);
        QVERIFY(pipeline);
        gst_object_ref_sink(pipeline);
        gst_object_ref(pipeline);  // kept to look at the state after the guard is gone
        {
            const PipelineGuard guard(pipeline);
            QVERIFY(guard);
            QVERIFY(gst_element_set_state(guard.get(), GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
            GstState state = GST_STATE_NULL;
            gst_element_get_state(guard.get(), &state, nullptr, 5 * GST_SECOND);
            QCOMPARE(state, GST_STATE_PLAYING);
        }
        GstState state = GST_STATE_PLAYING;
        gst_element_get_state(pipeline, &state, nullptr, 0);
        QCOMPARE(state, GST_STATE_NULL);
        QCOMPARE(refCount(pipeline), 1u);
        gst_object_unref(pipeline);
    }

    void pipelineGuardReset()
    {
        PipelineGuard guard;
        QVERIFY(!guard);
        GstElement* first = gst_pipeline_new("first");
        gst_object_ref_sink(first);
        gst_object_ref(first);
        guard.reset(first);
        QCOMPARE(refCount(first), 2u);
        guard.reset();
        QCOMPARE(refCount(first), 1u);
        QVERIFY(!guard);
        gst_object_unref(first);
    }

    void videoFrameMapGuardMapsAndUnmaps()
    {
        GstVideoInfo info;
        QVERIFY(gst_video_info_set_format(&info, GST_VIDEO_FORMAT_NV12, 64, 36));
        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, GST_VIDEO_INFO_SIZE(&info), nullptr);
        {
            const VideoFrameMapGuard map(&info, buffer, GST_MAP_WRITE);
            QVERIFY(map.isMapped());
            QCOMPARE(map.planeCount(), 2u);
            QCOMPARE(map.stride(0), 64);
            map.plane(0)[0] = 42;
            map.plane(1)[0] = 7;
            QCOMPARE(miniRefCount(buffer), 2u);  // the mapped frame holds a reference
        }
        GstMapInfo again;
        QVERIFY(gst_buffer_map(buffer, &again, GST_MAP_WRITE));
        QCOMPARE(again.data[0], guint8{42});
        gst_buffer_unmap(buffer, &again);
        QCOMPARE(miniRefCount(buffer), 1u);  // the mapping's reference is gone
        gst_buffer_unref(buffer);
    }
};

QTEST_GUILESS_MAIN(TestGstWrappers)
#include "tst_gst_wrappers.moc"
