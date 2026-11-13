#include "abrviz/core/io/mapped_file.h"

#include <QFile>

#include <utility>

namespace abrviz {

std::optional<MappedFile> MappedFile::open(const QString& path, QString* error)
{
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        if (error)
            *error = file->errorString();
        return std::nullopt;
    }
    const qint64 size = file->size();
    if (size == 0)
        return MappedFile(std::move(file), nullptr, 0);

    uchar* data = file->map(0, size);
    if (!data) {
        if (error)
            *error = file->errorString();
        return std::nullopt;
    }
    return MappedFile(std::move(file), data, size);
}

MappedFile::MappedFile(std::unique_ptr<QFile> file, uchar* data, qint64 size) noexcept
    : file_(std::move(file))
    , data_(data)
    , size_(size)
{
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : file_(std::move(other.file_))
    , data_(std::exchange(other.data_, nullptr))
    , size_(std::exchange(other.size_, 0))
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this != &other) {
        release();
        file_ = std::move(other.file_);
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

MappedFile::~MappedFile()
{
    release();
}

void MappedFile::release() noexcept
{
    if (file_ && data_)
        file_->unmap(data_);
    data_ = nullptr;
    size_ = 0;
    file_.reset();
}

} // namespace abrviz
