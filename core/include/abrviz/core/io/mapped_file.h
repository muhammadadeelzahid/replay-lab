#pragma once

#include <QByteArrayView>
#include <QString>

#include <memory>
#include <optional>

class QFile;

namespace abrviz {

/// A read-only memory mapping of a whole file. The mapping is released in the destructor.
///
/// Move-only: a moved-from object is empty and its destructor does nothing. The `QFile` is held
/// through a `std::unique_ptr` because `QFile` (a QObject) can be neither copied nor moved.
class MappedFile {
public:
    /// Maps `path`. Returns nothing and sets `error` if the file cannot be opened or mapped.
    /// An empty file gives a valid, empty mapping.
    static std::optional<MappedFile> open(const QString& path, QString* error = nullptr);

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile();

    [[nodiscard]] QByteArrayView view() const noexcept
    {
        return {reinterpret_cast<const char*>(data_), size_};
    }
    [[nodiscard]] qint64 size() const noexcept { return size_; }
    [[nodiscard]] bool isMapped() const noexcept { return data_ != nullptr; }

private:
    MappedFile(std::unique_ptr<QFile> file, uchar* data, qint64 size) noexcept;
    void release() noexcept;

    std::unique_ptr<QFile> file_;
    uchar* data_ = nullptr;
    qint64 size_ = 0;
};

} // namespace abrviz
