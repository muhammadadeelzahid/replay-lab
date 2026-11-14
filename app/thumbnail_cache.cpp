#include "thumbnail_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

constexpr int kMaxProcesses = 2;  ///< `ffmpeg` processes that may run at once.

} // namespace

ThumbnailCache::ThumbnailCache(QString ffmpeg, QString folder, QObject* parent)
    : QObject(parent)
    , ffmpeg_(std::move(ffmpeg))
    , folder_(folder.isEmpty()
                  ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                        .filePath(QStringLiteral("thumbnails"))
                  : std::move(folder))
{
}

ThumbnailCache::~ThumbnailCache()
{
    for (QProcess* process : std::as_const(running_)) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(2000);
    }
}

QString ThumbnailCache::fileFor(const QString& bundleDirectory) const
{
    const QByteArray key = QCryptographicHash::hash(QDir(bundleDirectory).absolutePath().toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(16);
    return QDir(folder_).filePath(QStringLiteral("%1-%2.jpg")
                                      .arg(QDir(bundleDirectory).dirName(), QString::fromLatin1(key)));
}

QString ThumbnailCache::sourceSegment(const QString& bundleDirectory, const QString& videoName,
                                      int segments, double position)
{
    if (videoName.isEmpty() || videoName.contains(QLatin1String("..")))
        return {};
    const QDir video(QDir(bundleDirectory).filePath(QStringLiteral("videos/") + videoName));
    // The rung folders are r0, r1, ...; the highest number is the best picture.
    int best = -1;
    for (const QString& name : video.entryList({QStringLiteral("r*")}, QDir::Dirs)) {
        bool ok = false;
        const int rung = name.mid(1).toInt(&ok);
        if (ok)
            best = std::max(best, rung);
    }
    if (best < 0)
        return {};
    const double where = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0.0;
    const int segment = std::clamp(static_cast<int>(where * segments), 0, std::max(0, segments - 1));
    for (const int candidate : {segment, 0}) {
        const QString file = video.filePath(
            QStringLiteral("r%1/seg_%2.mp4").arg(best).arg(candidate, 4, 10, QLatin1Char('0')));
        if (QFileInfo(file).isFile())
            return file;
    }
    return {};
}

void ThumbnailCache::request(const QString& bundleDirectory, const QString& videoName, int segments,
                             double position)
{
    if (images_.contains(bundleDirectory) || pending_.contains(bundleDirectory))
        return;
    const QString file = fileFor(bundleDirectory);
    if (QFileInfo(file).isFile()) {
        const QImage image(file);
        if (!image.isNull()) {
            images_.insert(bundleDirectory, image);
            emit ready(bundleDirectory);
            return;
        }
    }
    const QString source = sourceSegment(bundleDirectory, videoName, segments, position);
    if (source.isEmpty())
        return;
    pending_ << bundleDirectory;
    queue_.append({bundleDirectory, source});
    startNext();
}

QImage ThumbnailCache::image(const QString& bundleDirectory) const
{
    return images_.value(bundleDirectory);
}

void ThumbnailCache::startNext()
{
    while (running_.size() < kMaxProcesses && !queue_.isEmpty()) {
        const Job job = queue_.takeFirst();
        QDir().mkpath(folder_);
        const QString output = fileFor(job.bundleDirectory);
        auto* process = new QProcess(this);
        running_ << process;
        connect(process, &QProcess::finished, this, [this, process, job, output](int code, QProcess::ExitStatus status) {
            running_.removeOne(process);
            pending_.removeOne(job.bundleDirectory);
            process->deleteLater();
            const QImage image(output);
            if (code == 0 && status == QProcess::NormalExit && !image.isNull()) {
                images_.insert(job.bundleDirectory, image);
                emit ready(job.bundleDirectory);
            }
            startNext();
        });
        connect(process, &QProcess::errorOccurred, this, [this, process, job](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;  // other errors end in finished()
            running_.removeOne(process);
            pending_.removeOne(job.bundleDirectory);
            process->deleteLater();
            startNext();
        });
        // One frame, half a second into the segment, 640 pixels wide.
        process->start(ffmpeg_, {QStringLiteral("-nostdin"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                                 QStringLiteral("-y"), QStringLiteral("-ss"), QStringLiteral("0.5"),
                                 QStringLiteral("-i"), job.source, QStringLiteral("-frames:v"),
                                 QStringLiteral("1"), QStringLiteral("-vf"), QStringLiteral("scale=640:-2"),
                                 QStringLiteral("-q:v"), QStringLiteral("4"), output});
    }
}

} // namespace abrviz
