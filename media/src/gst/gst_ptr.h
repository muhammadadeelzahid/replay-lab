#pragma once

/**
 * @file
 * @brief RAII owners for GStreamer and GLib references.
 *
 * Only references we own go in here: the return value of gst_element_get_bus(),
 * gst_element_factory_make() after gst_object_ref_sink(), a pulled sample, a popped message.
 * Borrowed pointers (from a callback, or an element owned by a bin) stay raw. Wrapping a floating
 * reference and then adding it to a bin would release it twice.
 */

#include <gst/gst.h>

#include <memory>
#include <type_traits>

namespace abrviz::gst {

/**
 * @brief Deleter that releases a `GstObject` reference with gst_object_unref().
 */
struct ObjectUnref {
    /**
     * @brief Releases one reference.
     *
     * @param object The `GstObject` to unref; never null (`unique_ptr` skips null).
     */
    void operator()(gpointer object) const noexcept { gst_object_unref(object); }
};

/**
 * @brief An owned reference to a `GstObject` subclass (`GstElement`, `GstBus`, `GstPad`, ...).
 */
template <typename T>
using GstPtr = std::unique_ptr<T, ObjectUnref>;

/**
 * @brief Deleter that releases a `GstCaps` reference with gst_caps_unref().
 */
struct CapsUnref {
    /**
     * @brief Releases one reference.
     *
     * @param caps The caps to unref.
     */
    void operator()(GstCaps* caps) const noexcept { gst_caps_unref(caps); }
};
/**
 * @brief An owned reference to `GstCaps`.
 */
using GstCapsPtr = std::unique_ptr<GstCaps, CapsUnref>;

/**
 * @brief Deleter that releases a `GstSample` reference with gst_sample_unref().
 */
struct SampleUnref {
    /**
     * @brief Releases one reference.
     *
     * @param sample The sample to unref.
     */
    void operator()(GstSample* sample) const noexcept { gst_sample_unref(sample); }
};
/**
 * @brief An owned reference to a `GstSample`, such as one pulled from an appsink.
 */
using GstSamplePtr = std::unique_ptr<GstSample, SampleUnref>;

/**
 * @brief Deleter that releases a `GstMessage` reference with gst_message_unref().
 */
struct MessageUnref {
    /**
     * @brief Releases one reference.
     *
     * @param message The message to unref.
     */
    void operator()(GstMessage* message) const noexcept { gst_message_unref(message); }
};
/**
 * @brief An owned reference to a `GstMessage`, such as one popped from a bus.
 */
using GstMessagePtr = std::unique_ptr<GstMessage, MessageUnref>;

/**
 * @brief Deleter that frees GLib-allocated memory with g_free().
 */
struct GFree {
    /**
     * @brief Frees the memory.
     *
     * @param memory Memory allocated by GLib.
     */
    void operator()(gpointer memory) const noexcept { g_free(memory); }
};
/**
 * @brief A string allocated by GLib (`gst_caps_to_string`, the debug string of a `GError`, ...).
 */
using GCharPtr = std::unique_ptr<gchar, GFree>;

/**
 * @brief Deleter that frees a `GError` with g_error_free().
 */
struct ErrorFree {
    /**
     * @brief Frees the error.
     *
     * @param error The error to free.
     */
    void operator()(GError* error) const noexcept { g_error_free(error); }
};
/**
 * @brief An owned `GError`.
 */
using GErrorPtr = std::unique_ptr<GError, ErrorFree>;

/**
 * @brief Makes an element and takes ownership of it, sinking the floating reference.
 *
 * @param factory Name of the element factory, e.g. "appsink".
 * @param name Name of the new element; null lets GStreamer choose one.
 * @return The owned element, or null if the plugin is missing.
 */
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
