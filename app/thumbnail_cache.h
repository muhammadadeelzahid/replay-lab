#pragma once

/**
 * @file
 * @brief Still frames from the runs' videos, for the run cards and the featured run.
 */

#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>

class QProcess;

namespace abrviz {

/**
 * @brief Takes one still frame from each run's video with `ffmpeg` and keeps it on disk.
 *
 * The frame comes from a segment some way into the video (a third of the way by default), at
 * the highest quality rung, so it shows a typical picture rather than the opening titles. Frames are saved as JPEG
 * files in the cache folder, named after the bundle folder, and read back on the next request.
 * At most two `ffmpeg` processes run at once; destroying the cache stops them.
 */
class ThumbnailCache final : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Creates the cache.
     *
     * @param ffmpeg The `ffmpeg` program to run.
     * @param folder Where to keep the frames; created when the first one is saved. Empty for
     *               the user's cache folder.
     * @param parent The parent object.
     */
    ThumbnailCache(QString ffmpeg, QString folder, QObject* parent = nullptr);

    /// Stops the `ffmpeg` processes that are still running.
    ~ThumbnailCache() override;

    /**
     * @brief Asks for a bundle's frame.
     *
     * Emits ready() at once (before returning) if the frame is already on disk, or later when
     * `ffmpeg` has made it. Asking again for a frame that is being made does nothing.
     *
     * @param bundleDirectory The bundle folder.
     * @param videoName The video's folder name under `videos/` (`ues[0].video` of `run.json`).
     * @param segments The video's segment count.
     * @param position Where in the video to take the frame, from 0 (start) to 1 (end).
     */
    void request(const QString& bundleDirectory, const QString& videoName, int segments,
                 double position = 1.0 / 3.0);

    /**
     * @brief A bundle's frame.
     *
     * @param bundleDirectory The bundle folder.
     * @return The frame, or a null image if it is not ready (or could not be made).
     */
    [[nodiscard]] QImage image(const QString& bundleDirectory) const;

    /**
     * @brief The segment file a bundle's frame is taken from.
     *
     * @param bundleDirectory The bundle folder.
     * @param videoName The video's folder name under `videos/`.
     * @param segments The video's segment count.
     * @param position Where in the video, from 0 (start) to 1 (end).
     * @return The path of the segment at `position` at the highest rung found (the first
     *         segment if that one is missing); empty if the bundle has neither.
     */
    [[nodiscard]] static QString sourceSegment(const QString& bundleDirectory, const QString& videoName,
                                               int segments, double position);

signals:
    /**
     * @brief A bundle's frame is ready.
     *
     * @param bundleDirectory The bundle folder.
     */
    void ready(const QString& bundleDirectory);

private:
    /// The file a bundle's frame is kept in.
    [[nodiscard]] QString fileFor(const QString& bundleDirectory) const;

    /// Starts `ffmpeg` for queued bundles while fewer than two are running.
    void startNext();

    /// A frame still to be made.
    struct Job {
        QString bundleDirectory;  ///< the bundle folder
        QString source;           ///< the segment file to take the frame from
    };

    QString ffmpeg_;                   ///< the program to run
    QString folder_;                   ///< where frames are kept
    QHash<QString, QImage> images_;    ///< frames read so far, by bundle folder
    QStringList pending_;              ///< bundles queued or being made
    QList<Job> queue_;                 ///< frames waiting for a free process
    QList<QProcess*> running_;         ///< processes making frames
};

} // namespace abrviz
