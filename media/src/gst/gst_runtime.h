#pragma once

#include <QString>

namespace abrviz::gst {

/// Initialises GStreamer once per process (`gst_init_check` under `std::call_once`), from any
/// thread. Returns false, with the reason in `error`, if initialisation failed; the result of
/// the first call is kept.
bool ensureInitialized(QString* error = nullptr);

/// Whether the elements the replay pipeline needs are installed; lists the missing ones.
bool hasPipelineElements(QString* missing = nullptr);

} // namespace abrviz::gst
