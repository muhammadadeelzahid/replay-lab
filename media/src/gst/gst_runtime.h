#pragma once

/**
 * @file
 * @brief Process-wide GStreamer initialisation and the check for the pipeline's plugins.
 */

#include <QString>

namespace abrviz::gst {

/**
 * @brief Initialises GStreamer once per process (`gst_init_check` under `std::call_once`).
 *
 * Can be called from any thread. The result of the first call is kept and returned by every
 * later call.
 *
 * @param error Receives the reason if initialisation failed; may be null. Left untouched on
 *              success.
 * @return True if GStreamer is initialised, false if initialisation failed.
 */
bool ensureInitialized(QString* error = nullptr);

/**
 * @brief Whether the elements the replay pipeline needs are installed.
 *
 * Initialises GStreamer first (see `ensureInitialized()`). The elements checked are
 * splitmuxsrc, decodebin3, videoconvert, videoscale and appsink.
 *
 * @param missing Receives the initialisation error, or a message listing the missing elements;
 *                may be null. Left untouched when everything is present.
 * @return True if GStreamer initialised and every element is available.
 */
bool hasPipelineElements(QString* missing = nullptr);

} // namespace abrviz::gst
