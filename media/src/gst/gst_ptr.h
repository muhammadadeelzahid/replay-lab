#pragma once

// RAII owners for GStreamer references. Only references we own go in here: the return value of
// gst_element_get_bus(), gst_element_factory_make() after gst_object_ref_sink(), a pulled
// sample, a popped message. Borrowed pointers (from a callback, or an element owned by a bin)
// stay raw. Wrapping a floating reference and then adding it to a bin would release it twice.

#include <gst/gst.h>

#include <memory>
#include <type_traits>

namespace abrviz::gst {

struct ObjectUnref {
    void operator()(gpointer object) const noexcept { gst_object_unref(object); }
};

/// An owned reference to a `GstObject` subclass (`GstElement`, `GstBus`, `GstPad`, ...).
template <typename T>
using GstPtr = std::unique_ptr<T, ObjectUnref>;

struct CapsUnref {
    void operator()(GstCaps* caps) const noexcept { gst_caps_unref(caps); }
};
using GstCapsPtr = std::unique_ptr<GstCaps, CapsUnref>;

struct SampleUnref {
    void operator()(GstSample* sample) const noexcept { gst_sample_unref(sample); }
};
using GstSamplePtr = std::unique_ptr<GstSample, SampleUnref>;

struct MessageUnref {
    void operator()(GstMessage* message) const noexcept { gst_message_unref(message); }
};
using GstMessagePtr = std::unique_ptr<GstMessage, MessageUnref>;

struct GFree {
    void operator()(gpointer memory) const noexcept { g_free(memory); }
};
/// A string allocated by GLib (`gst_caps_to_string`, the debug string of a `GError`, ...).
using GCharPtr = std::unique_ptr<gchar, GFree>;

struct ErrorFree {
    void operator()(GError* error) const noexcept { g_error_free(error); }
};
using GErrorPtr = std::unique_ptr<GError, ErrorFree>;

/// Makes an element and takes ownership of it (sinking the floating reference), or returns null
/// if the plugin is missing.
[[nodiscard]] inline GstPtr<GstElement> makeElement(const char* factory, const char* name = nullptr)
{
    GstElement* element = gst_element_factory_make(factory, name);
    if (element)
        gst_object_ref_sink(element);
    return GstPtr<GstElement>(element);
}

static_assert(!std::is_copy_constructible_v<GstPtr<GstElement>>);
static_assert(std::is_nothrow_move_constructible_v<GstPtr<GstElement>>);
static_assert(!std::is_copy_constructible_v<GstCapsPtr>);
static_assert(std::is_nothrow_move_constructible_v<GstSamplePtr>);
static_assert(std::is_nothrow_move_constructible_v<GstMessagePtr>);

} // namespace abrviz::gst
