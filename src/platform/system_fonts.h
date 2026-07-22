#pragma once

#include <cstddef>

// Runtime discovery of a system font that covers CJK glyphs, so the app can
// merge it over the bundled Roboto without ever hard-coding a font name or
// path. Windows uses DirectWrite's font-fallback service; POSIX/Linux uses
// fontconfig. Both resolve to a file on disk, which is then memory-mapped
// rather than read into a heap buffer, so the OS only pages in the bytes the
// rasterizer actually touches instead of committing the whole (often
// 15-40 MB) font file to RAM.
namespace platform {

// A runtime-discovered system font, memory-mapped read-only. Move-only;
// unmaps on destruction. valid() == false means "not found" — callers should
// skip the merge rather than treat this as an error.
class MappedFont {
public:
    MappedFont() = default;
    ~MappedFont();

    MappedFont(MappedFont&& other) noexcept;
    MappedFont& operator=(MappedFont&& other) noexcept;

    MappedFont(const MappedFont&) = delete;
    MappedFont& operator=(const MappedFont&) = delete;

    const void* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool valid() const { return data_ != nullptr && size_ > 0; }

private:
    void unmap();

    const void* data_ = nullptr;
    std::size_t size_ = 0;

#ifdef _WIN32
    void* file_handle_ = nullptr;    // HANDLE, kept opaque to avoid <windows.h> here
    void* mapping_handle_ = nullptr; // HANDLE
    static MappedFont mapPath(const wchar_t* path);
#else
    int fd_ = -1;
    static MappedFont mapPath(const char* path);
#endif

    friend MappedFont LoadCjkSystemFont();
    friend MappedFont LoadCjkSystemFontBold();
};

// Asks the OS to pick a font covering a representative CJK code point
// (U+4E2D, "中"), resolves it to a file on disk, and memory-maps it. Returns
// an invalid MappedFont when no such font can be found or resolved — this is
// a normal, non-fatal outcome (e.g. a minimal Linux install with no CJK
// fonts installed), never an exception or a hard failure.
MappedFont LoadCjkSystemFont();

// Same discovery as LoadCjkSystemFont(), but asks for a BOLD weight (e.g.
// Microsoft YaHei Bold) instead of normal. Returns an invalid MappedFont when
// the OS has no bold CJK face to offer — also normal and non-fatal; callers
// should fall back to the normal-weight face or faux-bold in that case.
MappedFont LoadCjkSystemFontBold();

} // namespace platform
