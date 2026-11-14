#include "gst_runtime.h"

#include "gst_ptr.h"

#include <QStringList>

#include <mutex>

namespace abrviz::gst {

namespace {

/**
 * @brief The outcome of `gst_init_check`, kept for the whole process.
 */
struct InitResult {
    bool ok = false;  ///< whether `gst_init_check` succeeded
    QString error;  ///< the reason initialisation failed; empty on success
};

/**
 * @brief Initialises GStreamer on the first call and returns the stored outcome.
 *
 * Thread-safe: the initialisation runs once under `std::call_once`.
 *
 * @return The result of the one `gst_init_check` call, valid for the life of the process.
 */
const InitResult& initResult()
{
    static std::once_flag once;
    static InitResult result;
    std::call_once(once, [] {
        GError* raw = nullptr;
        result.ok = gst_init_check(nullptr, nullptr, &raw) != FALSE;
        const GErrorPtr error(raw);
        if (!result.ok)
            result.error = error ? QString::fromUtf8(error->message)
                                 : QStringLiteral("gst_init failed");
    });
    return result;
}

} // namespace

bool ensureInitialized(QString* error)
{
    const InitResult& result = initResult();
    if (!result.ok && error)
        *error = result.error;
    return result.ok;
}

bool hasPipelineElements(QString* missing)
{
    if (!ensureInitialized(missing))
        return false;
    QStringList absent;
    for (const char* name : {"splitmuxsrc", "decodebin3", "videoconvert", "videoscale", "appsink"}) {
        const GstPtr<GstElementFactory> factory(gst_element_factory_find(name));
        if (!factory)
            absent << QString::fromLatin1(name);
    }
    if (missing && !absent.isEmpty())
        *missing = QStringLiteral("missing GStreamer elements: ") + absent.join(QStringLiteral(", "));
    return absent.isEmpty();
}

} // namespace abrviz::gst
