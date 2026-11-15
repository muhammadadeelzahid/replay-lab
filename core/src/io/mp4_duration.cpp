#include "abrviz/core/io/mp4_duration.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <array>

namespace abrviz {

namespace {

/** @brief A four-character MP4 box type, such as `moov`. */
using FourCC = std::array<char, 4>;

/**
 * @brief Position and type of one MP4 box.
 */
struct Box {
    FourCC type{};  ///< four-character box type, for example `moov`
    qint64 payloadStart = 0;  ///< offset after the header
    qint64 end = 0;           ///< offset after the box
};

/**
 * @brief Reads a big-endian integer at the current file position.
 *
 * @tparam T Unsigned integer type to read.
 * @param file The open file.
 * @return The value, or nothing if fewer than `sizeof(T)` bytes could be read.
 */
template <typename T>
std::optional<T> readBigEndian(QFile& file)
{
    std::array<uchar, sizeof(T)> bytes{};
    if (file.read(reinterpret_cast<char*>(bytes.data()), sizeof(T)) != sizeof(T))
        return std::nullopt;
    return qFromBigEndian<T>(bytes.data());
}

/**
 * @brief Reads the header of the box at `offset`.
 *
 * Handles 64-bit sizes (size 1) and boxes that extend to the end of their parent (size 0).
 *
 * @param file The open file.
 * @param offset File offset of the box header.
 * @param limit End of the parent box or of the file; the box must not extend past it.
 * @return The box, or nothing if the header cannot be read or the size is invalid.
 */
std::optional<Box> readBox(QFile& file, qint64 offset, qint64 limit)
{
    if (offset + 8 > limit || !file.seek(offset))
        return std::nullopt;
    const auto size32 = readBigEndian<quint32>(file);
    Box box;
    if (!size32 || file.read(box.type.data(), 4) != 4)
        return std::nullopt;

    qint64 size = *size32;
    box.payloadStart = offset + 8;
    if (size == 1) {
        const auto size64 = readBigEndian<quint64>(file);
        if (!size64)
            return std::nullopt;
        size = static_cast<qint64>(*size64);
        box.payloadStart += 8;
    } else if (size == 0) {
        size = limit - offset;  // the box extends to the end of its parent
    }
    // `size > limit - offset` rather than `offset + size > limit`: a 64-bit size near the
    // maximum would overflow the sum.
    if (size < box.payloadStart - offset || size > limit - offset)
        return std::nullopt;
    box.end = offset + size;
    return box;
}

/**
 * @brief Makes a box type from a four-character string literal.
 *
 * @param text Four characters plus the terminating null.
 * @return The box type.
 */
constexpr FourCC fourCC(const char (&text)[5])
{
    return {text[0], text[1], text[2], text[3]};
}

/**
 * @brief Returns the first box in the range `[from, to)` for which `matches(box)` is true.
 *
 * @param file The open file.
 * @param from Offset of the first box.
 * @param to End of the range.
 * @param matches Predicate called for each box in order.
 * @return The matching box, or nothing if none matches or a box header cannot be read.
 */
template <typename Fn>
std::optional<Box> findChild(QFile& file, qint64 from, qint64 to, Fn&& matches)
{
    qint64 offset = from;
    while (auto box = readBox(file, offset, to)) {
        if (matches(*box))
            return box;
        offset = box->end;
    }
    return std::nullopt;
}

/**
 * @brief Returns the first child of `parent` with the given type.
 *
 * @param file The open file.
 * @param parent The box to search in.
 * @param type The box type to look for.
 * @return The child, or nothing if there is none.
 */
std::optional<Box> findChild(QFile& file, const Box& parent, FourCC type)
{
    return findChild(file, parent.payloadStart, parent.end,
                     [type](const Box& box) { return box.type == type; });
}

/**
 * @brief Reads the timescale and duration of an `mvhd` or `mdhd` box (same layout for both).
 *
 * @param file The open file.
 * @param header The `mvhd` or `mdhd` box.
 * @return The duration in seconds, or nothing if it cannot be read or the timescale is 0.
 */
std::optional<double> readHeaderDuration(QFile& file, const Box& header)
{
    if (!file.seek(header.payloadStart))
        return std::nullopt;
    const auto versionAndFlags = readBigEndian<quint32>(file);
    if (!versionAndFlags)
        return std::nullopt;
    const bool version1 = (*versionAndFlags >> 24) == 1;
    // The fields must lie inside the box, not in whatever follows it.
    if (header.end - header.payloadStart < (version1 ? 32 : 20))
        return std::nullopt;

    // version 0: creation(4) modification(4) timescale(4) duration(4)
    // version 1: creation(8) modification(8) timescale(4) duration(8)
    if (!file.seek(file.pos() + (version1 ? 16 : 8)))
        return std::nullopt;
    const auto timescale = readBigEndian<quint32>(file);
    std::optional<quint64> duration;
    if (version1)
        duration = readBigEndian<quint64>(file);
    else if (const auto d32 = readBigEndian<quint32>(file))
        duration = *d32;
    if (!timescale || *timescale == 0 || !duration)
        return std::nullopt;
    return static_cast<double>(*duration) / static_cast<double>(*timescale);
}

/**
 * @brief Tells whether a `trak` box is a video track.
 *
 * @param file The open file.
 * @param trak The `trak` box.
 * @return True if its `mdia/hdlr` handler type is `vide`.
 */
bool isVideoTrack(QFile& file, const Box& trak)
{
    const auto mdia = findChild(file, trak, fourCC("mdia"));
    if (!mdia)
        return false;
    const auto hdlr = findChild(file, *mdia, fourCC("hdlr"));
    // hdlr: version and flags(4) pre_defined(4) handler_type(4)
    if (!hdlr || hdlr->end - hdlr->payloadStart < 12 || !file.seek(hdlr->payloadStart + 8))
        return false;
    FourCC handler{};
    return file.read(handler.data(), 4) == 4 && handler == fourCC("vide");
}

} // namespace

std::optional<double> readMp4Duration(const QString& path)
{
    // Only regular files: opening a FIFO for reading would block until a writer appears.
    if (!QFileInfo(path).isFile())
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    const auto moov = findChild(file, 0, file.size(),
                                [](const Box& box) { return box.type == fourCC("moov"); });
    if (!moov)
        return std::nullopt;

    // The video track's media header gives the duration of the frames themselves. The movie
    // header can be longer, because it also counts the presentation offset of the edit list.
    const auto videoTrak = findChild(file, moov->payloadStart, moov->end, [&](const Box& box) {
        return box.type == fourCC("trak") && isVideoTrack(file, box);
    });
    if (videoTrak) {
        if (const auto mdia = findChild(file, *videoTrak, fourCC("mdia"))) {
            if (const auto mdhd = findChild(file, *mdia, fourCC("mdhd")))
                return readHeaderDuration(file, *mdhd);
        }
    }
    if (const auto mvhd = findChild(file, *moov, fourCC("mvhd")))
        return readHeaderDuration(file, *mvhd);
    return std::nullopt;
}

} // namespace abrviz
