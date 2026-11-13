#include "abrviz/core/io/mp4_duration.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <array>

namespace abrviz {

namespace {

using FourCC = std::array<char, 4>;

struct Box {
    FourCC type{};
    qint64 payloadStart = 0;  ///< offset after the header
    qint64 end = 0;           ///< offset after the box
};

template <typename T>
std::optional<T> readBigEndian(QFile& file)
{
    std::array<uchar, sizeof(T)> bytes{};
    if (file.read(reinterpret_cast<char*>(bytes.data()), sizeof(T)) != sizeof(T))
        return std::nullopt;
    return qFromBigEndian<T>(bytes.data());
}

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
    if (size < box.payloadStart - offset || offset + size > limit)
        return std::nullopt;
    box.end = offset + size;
    return box;
}

constexpr FourCC fourCC(const char (&text)[5])
{
    return {text[0], text[1], text[2], text[3]};
}

/// Calls `fn(box)` for each child of the range `[from, to)` until it returns true.
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

std::optional<Box> findChild(QFile& file, const Box& parent, FourCC type)
{
    return findChild(file, parent.payloadStart, parent.end,
                     [type](const Box& box) { return box.type == type; });
}

/// Reads the timescale and duration of an `mvhd` or `mdhd` box (same layout for both).
std::optional<double> readHeaderDuration(QFile& file, const Box& header)
{
    if (!file.seek(header.payloadStart))
        return std::nullopt;
    const auto versionAndFlags = readBigEndian<quint32>(file);
    if (!versionAndFlags)
        return std::nullopt;
    const bool version1 = (*versionAndFlags >> 24) == 1;

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

bool isVideoTrack(QFile& file, const Box& trak)
{
    const auto mdia = findChild(file, trak, fourCC("mdia"));
    if (!mdia)
        return false;
    const auto hdlr = findChild(file, *mdia, fourCC("hdlr"));
    // hdlr: version and flags(4) pre_defined(4) handler_type(4)
    if (!hdlr || !file.seek(hdlr->payloadStart + 8))
        return false;
    FourCC handler{};
    return file.read(handler.data(), 4) == 4 && handler == fourCC("vide");
}

} // namespace

std::optional<double> readMp4Duration(const QString& path)
{
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
