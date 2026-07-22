#include "platform/system_fonts.h"

#ifdef _WIN32

// Require Windows 10 APIs (GetUserDefaultLocaleName is Vista+, but keep in
// step with win32_chrome.cpp's floor).
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

// DEFINE_GUID normally only declares an `extern` GUID and expects a .lib to
// supply the storage; mingw's import libs don't carry the DirectWrite IIDs.
// <initguid.h> flips DEFINE_GUID to actually define (selectany) storage, so
// the IID_* symbols dwrite.h emits are self-contained. Must precede dwrite.h.
#include <initguid.h>
#include <dwrite_2.h>

#include <string>

#else // POSIX

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fontconfig/fontconfig.h>

#include <cstring>

#endif

namespace platform {

MappedFont::~MappedFont() { unmap(); }

MappedFont::MappedFont(MappedFont&& other) noexcept
    : data_(other.data_), size_(other.size_)
#ifdef _WIN32
    , file_handle_(other.file_handle_), mapping_handle_(other.mapping_handle_)
#else
    , fd_(other.fd_)
#endif
{
    other.data_ = nullptr;
    other.size_ = 0;
#ifdef _WIN32
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

MappedFont& MappedFont::operator=(MappedFont&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    unmap();
    data_ = other.data_;
    size_ = other.size_;
#ifdef _WIN32
    file_handle_ = other.file_handle_;
    mapping_handle_ = other.mapping_handle_;
#else
    fd_ = other.fd_;
#endif
    other.data_ = nullptr;
    other.size_ = 0;
#ifdef _WIN32
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
    return *this;
}

void MappedFont::unmap() {
#ifdef _WIN32
    if (data_ != nullptr) {
        UnmapViewOfFile(data_);
    }
    if (mapping_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mapping_handle_));
    }
    if (file_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(file_handle_));
    }
    data_ = nullptr;
    size_ = 0;
    file_handle_ = nullptr;
    mapping_handle_ = nullptr;
#else
    if (data_ != nullptr && size_ > 0) {
        munmap(const_cast<void*>(data_), size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
    data_ = nullptr;
    size_ = 0;
    fd_ = -1;
#endif
}

#ifdef _WIN32

MappedFont MappedFont::mapPath(const wchar_t* path) {
    MappedFont font;

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return font;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        return font;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        return font;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        return font;
    }

    font.data_ = view;
    font.size_ = static_cast<std::size_t>(size.QuadPart);
    font.file_handle_ = file;
    font.mapping_handle_ = mapping;
    return font;
}

namespace {

// Minimal IDWriteTextAnalysisSource that hands MapCharacters a single code
// point plus a locale name. Lives on the stack for the duration of one
// MapCharacters() call, so AddRef/Release are safe no-ops (DirectWrite's
// use of the object never outlives that call).
class SingleCharSource : public IDWriteTextAnalysisSource {
public:
    SingleCharSource(const wchar_t* text, const wchar_t* locale) : text_(text), locale_(locale) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IDWriteTextAnalysisSource) || riid == __uuidof(IUnknown)) {
            *object = static_cast<IDWriteTextAnalysisSource*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
                                                 UINT32* text_len) override {
        if (position >= 1) {
            *text = nullptr;
            *text_len = 0;
            return S_OK;
        }
        *text = text_;
        *text_len = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 /*position*/, const WCHAR** text,
                                                     UINT32* text_len) override {
        *text = nullptr;
        *text_len = 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 /*position*/, UINT32* text_len,
                                             const WCHAR** locale) override {
        *locale = locale_;
        *text_len = 1; // locale applies for the whole (single-character) run
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 /*position*/, UINT32* text_len,
                                                     IDWriteNumberSubstitution** substitution) override {
        *substitution = nullptr;
        *text_len = 1;
        return S_OK;
    }

private:
    const wchar_t* text_;
    const wchar_t* locale_;
};

// Walks IDWriteFont -> IDWriteFontFace -> IDWriteFontFile -> local file path.
// Fails (returns false) for fonts DirectWrite can't hand us a filesystem path
// for (e.g. a memory/remote font collection) rather than guessing one.
bool resolve_font_path(IDWriteFont* font, std::wstring& out_path) {
    IDWriteFontFace* face = nullptr;
    if (FAILED(font->CreateFontFace(&face)) || face == nullptr) {
        return false;
    }

    UINT32 file_count = 1;
    IDWriteFontFile* file = nullptr;
    // A collection (.ttc) face may reference more than one file; we only ask
    // for the first. FontNo/face-index selection is not plumbed through
    // ImGuiMD2::FontMerge, so a CJK face that isn't the first in its
    // collection is a known limitation of this discovery path.
    HRESULT hr = face->GetFiles(&file_count, &file);
    face->Release();
    if (FAILED(hr) || file == nullptr) {
        return false;
    }

    bool ok = false;
    IDWriteFontFileLoader* loader = nullptr;
    if (SUCCEEDED(file->GetLoader(&loader)) && loader != nullptr) {
        IDWriteLocalFontFileLoader* local_loader = nullptr;
        if (SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                              reinterpret_cast<void**>(&local_loader))) &&
            local_loader != nullptr) {
            const void* key = nullptr;
            UINT32 key_size = 0;
            if (SUCCEEDED(file->GetReferenceKey(&key, &key_size))) {
                UINT32 path_len = 0;
                if (SUCCEEDED(local_loader->GetFilePathLengthFromKey(key, key_size, &path_len))) {
                    out_path.assign(static_cast<std::size_t>(path_len) + 1, L'\0');
                    if (SUCCEEDED(local_loader->GetFilePathFromKey(
                            key, key_size, &out_path[0], path_len + 1))) {
                        out_path.resize(path_len);
                        ok = true;
                    }
                }
            }
            local_loader->Release();
        }
        loader->Release();
    }
    file->Release();
    return ok;
}

// Primary discovery: ask DirectWrite's system font-fallback service which
// font it would use to render U+4E2D ("中") in the user's locale. This is
// the same mechanism DirectWrite text layout uses internally, so it reflects
// whatever CJK font (if any) the OS/user has actually configured — no name
// or path is ever hard-coded here.
IDWriteFont* find_fallback_font(IDWriteFactory* factory, const wchar_t* locale,
                                 DWRITE_FONT_WEIGHT weight) {
    IDWriteFactory2* factory2 = nullptr;
    if (FAILED(factory->QueryInterface(__uuidof(IDWriteFactory2),
                                        reinterpret_cast<void**>(&factory2))) ||
        factory2 == nullptr) {
        return nullptr; // IDWriteFactory2 needs Windows 8.1+; degrade below.
    }

    IDWriteFontFallback* fallback = nullptr;
    HRESULT hr = factory2->GetSystemFontFallback(&fallback);
    factory2->Release();
    if (FAILED(hr) || fallback == nullptr) {
        return nullptr;
    }

    static const wchar_t kProbe[] = {0x4E2D, 0}; // "中"
    SingleCharSource source(kProbe, locale);

    UINT32 mapped_length = 0;
    IDWriteFont* font = nullptr;
    FLOAT scale = 1.0f;
    hr = fallback->MapCharacters(&source, 0, 1, nullptr, nullptr, weight,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                  &mapped_length, &font, &scale);
    fallback->Release();
    if (FAILED(hr) || font == nullptr) {
        return nullptr;
    }
    return font;
}

// Degraded path for when GetSystemFontFallback isn't available (pre-8.1):
// ask GDI interop for whatever font the system resolves an unnamed,
// default-charset LOGFONT to. Still no hard-coded name — it's whatever the
// OS picks — but unlike MapCharacters it isn't guaranteed to cover CJK.
IDWriteFont* find_gdi_default_font(IDWriteFactory* factory, LONG gdi_weight) {
    IDWriteGdiInterop* interop = nullptr;
    if (FAILED(factory->GetGdiInterop(&interop)) || interop == nullptr) {
        return nullptr;
    }

    LOGFONTW logfont{};
    logfont.lfHeight = -12;
    logfont.lfWeight = gdi_weight;
    logfont.lfCharSet = DEFAULT_CHARSET; // lfFaceName left empty: let the OS pick.

    IDWriteFont* font = nullptr;
    const HRESULT hr = interop->CreateFontFromLOGFONT(&logfont, &font);
    interop->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    return font;
}

// Shared discovery logic for LoadCjkSystemFont()/LoadCjkSystemFontBold():
// only the requested weight differs between the two. Returns the resolved
// file path (empty on failure) rather than a MappedFont directly, since
// MappedFont::mapPath() is private to the two friend functions below.
std::wstring FindCjkSystemFontPath(DWRITE_FONT_WEIGHT weight, LONG gdi_weight) {
    IDWriteFactory* factory = nullptr;
    // DWriteCreateFactory does not require CoInitialize.
    const HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown**>(&factory));
    if (FAILED(hr) || factory == nullptr) {
        return std::wstring();
    }

    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"";
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) == 0) {
        locale[0] = L'\0'; // empty locale name is valid: DirectWrite treats it as invariant
    }

    IDWriteFont* font = find_fallback_font(factory, locale, weight);
    if (font == nullptr) {
        font = find_gdi_default_font(factory, gdi_weight);
    }
    if (font == nullptr) {
        factory->Release();
        return std::wstring();
    }

    std::wstring path;
    const bool resolved = resolve_font_path(font, path);
    font->Release();
    factory->Release();
    if (!resolved) {
        return std::wstring();
    }
    return path;
}

} // namespace

MappedFont LoadCjkSystemFont() {
    const std::wstring path = FindCjkSystemFontPath(DWRITE_FONT_WEIGHT_NORMAL, FW_NORMAL);
    if (path.empty()) return MappedFont();
    return MappedFont::mapPath(path.c_str());
}

MappedFont LoadCjkSystemFontBold() {
    const std::wstring path = FindCjkSystemFontPath(DWRITE_FONT_WEIGHT_BOLD, FW_BOLD);
    if (path.empty()) return MappedFont();
    return MappedFont::mapPath(path.c_str());
}

#else // POSIX

MappedFont MappedFont::mapPath(const char* path) {
    MappedFont font;

    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return font;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return font;
    }

    const std::size_t size = static_cast<std::size_t>(st.st_size);
    void* view = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        close(fd);
        return font;
    }
    madvise(view, size, MADV_RANDOM); // pages come in scattered (cmap/loca/glyf), not sequentially

    font.data_ = view;
    font.size_ = size;
    font.fd_ = fd;
    return font;
}

namespace {

// Shared discovery logic for LoadCjkSystemFont()/LoadCjkSystemFontBold():
// match a font covering `:lang=zh-cn`, optionally constrained to bold, which
// is resolved from the user's installed fonts and fontconfig configuration —
// never a hard-coded family name or path.
MappedFont LoadCjkSystemFontImpl(bool bold) {
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (config == nullptr) {
        return MappedFont();
    }

    FcPattern* pattern = FcPatternCreate();
    if (pattern == nullptr) {
        FcConfigDestroy(config);
        return MappedFont();
    }
    FcPatternAddString(pattern, FC_LANG, reinterpret_cast<const FcChar8*>("zh-cn"));
    if (bold) {
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    }
    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    MappedFont font;
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(config, pattern, &result);
    if (match != nullptr) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
            font = MappedFont::mapPath(reinterpret_cast<const char*>(file));
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    FcConfigDestroy(config);
    return font;
}

} // namespace

MappedFont LoadCjkSystemFont() { return LoadCjkSystemFontImpl(false); }

MappedFont LoadCjkSystemFontBold() { return LoadCjkSystemFontImpl(true); }

#endif

} // namespace platform
