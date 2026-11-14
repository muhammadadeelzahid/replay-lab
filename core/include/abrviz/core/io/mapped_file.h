#pragma once

/**
 * @file
 * @brief Read-only memory mapping of a whole file.
 */

#include <QByteArrayView>
#include <QString>

#include <memory>
#include <optional>

class QFile;

namespace abrviz {

/**
 * @brief A read-only memory mapping of a whole file.
 *
 * The mapping is released in the destructor. Move-only: a moved-from object is empty and its
 * destructor does nothing. The `QFile` is held through a `std::unique_ptr` because `QFile`
 * (a QObject) can be neither copied nor moved.
 */
class MappedFile {
public:
    /**
     * @brief Opens and maps a file.
     *
     * An empty file gives a valid, empty mapping.
     *
     * @param path Path of the file.
     * @param error Receives the reason on failure; may be null.
     * @return The mapping, or nothing if the file cannot be opened or mapped.
     */
    static std::optional<MappedFile> open(const QString& path, QString* error = nullptr);

    /**
     * @brief Takes over the mapping of `other`, which is left empty.
     *
     * @param other The mapping to move from.
     */
    MappedFile(MappedFile&& other) noexcept;

    /**
     * @brief Releases the current mapping and takes over the mapping of `other`.
     *
     * `other` is left empty. Self-assignment does nothing.
     *
     * @param other The mapping to move from.
     * @return This object.
     */
    MappedFile& operator=(MappedFile&& other) noexcept;

    /** @brief Not copyable: the mapping has a single owner. */
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    /**
     * @brief Unmaps the file and closes it.
     */
    ~MappedFile();

    /**
     * @brief Returns the mapped bytes.
     *
     * @return A view of the whole file; empty for an empty or moved-from mapping.
     */
    [[nodiscard]] QByteArrayView view() const noexcept
    {
        return {reinterpret_cast<const char*>(data_), size_};
    }

    /**
     * @brief Returns the size of the mapping.
     *
     * @return The size in bytes; 0 for an empty or moved-from mapping.
     */
    [[nodiscard]] qint64 size() const noexcept { return size_; }

    /**
     * @brief Tells whether memory is mapped.
     *
     * @return False for an empty file and for a moved-from object.
     */
    [[nodiscard]] bool isMapped() const noexcept { return data_ != nullptr; }

private:
    /**
     * @brief Takes ownership of an open file and its mapping.
     *
     * @param file The open file.
     * @param data Start of the mapping, or null for an empty file.
     * @param size Size of the mapping in bytes.
     */
    MappedFile(std::unique_ptr<QFile> file, uchar* data, qint64 size) noexcept;

    /**
     * @brief Unmaps the memory, closes the file and leaves the object empty.
     */
    void release() noexcept;

    std::unique_ptr<QFile> file_;  ///< the open file; null once released or moved from
    /** @brief Start of the mapping; null for an empty file, or once released or moved from. */
    uchar* data_ = nullptr;
    qint64 size_ = 0;              ///< size of the mapping in bytes
};

} // namespace abrviz
