#include "gst_runtime.h"

#include "gst_ptr.h"

#include <QStringList>

#include <mutex>

namespace abrviz::gst {

namespace {

struct InitResult {
    bool ok = false;
    QString error;
};

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
