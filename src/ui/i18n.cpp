#include "ui/i18n.h"

#include "ui/i18n_generated.h"

#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace ui::i18n {
namespace {

// Current / fallback language, as indices into i18n_gen's language table.
// Render-thread-only, same convention as ui::theme's global Theme -- see
// i18n.h's threading note.
int g_current_lang = 0;
int g_fallback_lang = 0; // index of "en", resolved once in Initialize()

// Lowercases ASCII letters and folds '_' to '-', so BCP-47-ish tags compare
// equal regardless of case or separator style (e.g. "zh_CN" == "zh-cn").
std::string NormalizeTag(std::string tag) {
    for (char& c : tag) {
        if (c == '_') {
            c = '-';
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return tag;
}

std::string PrimarySubtag(const std::string& normalized) {
    const std::size_t dash = normalized.find('-');
    return dash == std::string::npos ? normalized : normalized.substr(0, dash);
}

// Exact match first, then primary-subtag match (e.g. "zh-Hans-CN" doesn't
// exactly match embedded code "zh-CN", but both share primary subtag "zh").
// Returns -1 if nothing embedded matches at all.
int FindLanguageIndex(const std::string& locale) {
    if (locale.empty()) return -1;
    const std::string want = NormalizeTag(locale);
    const int count = i18n_gen::LanguageCount();

    for (int i = 0; i < count; ++i) {
        const char* code = i18n_gen::LangCode(i);
        if (code != nullptr && NormalizeTag(code) == want) return i;
    }

    const std::string want_primary = PrimarySubtag(want);
    for (int i = 0; i < count; ++i) {
        const char* code = i18n_gen::LangCode(i);
        if (code != nullptr && PrimarySubtag(NormalizeTag(code)) == want_primary) return i;
    }

    return -1;
}

// Binary search over i18n_gen::Key(), which is emitted in ascending order.
int FindKeyIndex(const char* key) {
    int lo = 0;
    int hi = i18n_gen::KeyCount() - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const int cmp = std::strcmp(i18n_gen::Key(mid), key);
        if (cmp == 0) return mid;
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

} // namespace

void Initialize(const char* preferred_locale) {
    const int count = i18n_gen::LanguageCount();

    int fallback = 0;
    for (int i = 0; i < count; ++i) {
        const char* code = i18n_gen::LangCode(i);
        if (code != nullptr && NormalizeTag(code) == "en") {
            fallback = i;
            break;
        }
    }
    g_fallback_lang = fallback;

    const std::string locale =
        preferred_locale != nullptr ? std::string(preferred_locale) : DetectSystemLocale();
    const int match = FindLanguageIndex(locale);
    g_current_lang = match >= 0 ? match : g_fallback_lang;
}

const char* Tr(const char* key) {
    const int key_index = FindKeyIndex(key);
    if (key_index < 0) return key; // unknown key: surface it verbatim so it's easy to spot

    const char* value = i18n_gen::Value(g_current_lang, key_index);
    if (value != nullptr) return value;

    value = i18n_gen::Value(g_fallback_lang, key_index);
    if (value != nullptr) return value;

    return key;
}

std::string TrLabel(const char* key, const char* stable_id) {
    std::string label(Tr(key));
    label += "###";
    label += stable_id;
    return label;
}

const char* CurrentLanguageCode() { return i18n_gen::LangCode(g_current_lang); }

int LanguageCount() { return i18n_gen::LanguageCount(); }
const char* LanguageDisplayName(int i) { return i18n_gen::LangName(i); }
const char* LanguageCode(int i) { return i18n_gen::LangCode(i); }
int CurrentLanguageIndex() { return g_current_lang; }

void SetLanguageIndex(int i) {
    if (i >= 0 && i < i18n_gen::LanguageCount()) {
        g_current_lang = i;
    }
}

#ifdef _WIN32

std::string DetectSystemLocale() {
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH) == 0) {
        return std::string();
    }
    // BCP-47-ish locale names (e.g. "zh-CN") are pure ASCII, so a per-char
    // narrowing cast is safe here -- no need to pull in a full wide->UTF-8
    // conversion path just for this.
    std::string out;
    for (const wchar_t* p = buffer; *p != L'\0'; ++p) {
        out.push_back(static_cast<char>(*p));
    }
    return out;
}

#else // POSIX

std::string DetectSystemLocale() {
    static const char* const kVars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* var : kVars) {
        const char* value = std::getenv(var);
        if (value == nullptr || value[0] == '\0') continue;
        std::string locale(value);
        const std::size_t cut = locale.find_first_of(".@");
        if (cut != std::string::npos) {
            locale.resize(cut);
        }
        if (!locale.empty()) return locale;
    }
    return std::string();
}

#endif

} // namespace ui::i18n
