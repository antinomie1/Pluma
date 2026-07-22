#pragma once

#include <string>

// Runtime-facing i18n API. Backed by src/ui/i18n_generated.{h,cpp} -- a byte
// table compiled into the binary from assets/lang/*.xml by
// scripts/generate_embedded_languages.ps1 -- so there is no XML parsing, and
// no new library dependency, at runtime.
//
// Threading: mirrors the ui::theme (GetTheme()/SetTheme()) convention --
// Initialize() runs once on the render thread before the first frame
// (render/renderer.cpp), and every other function here is only ever called
// from that same thread while building a frame (src/ui/frame.cpp). No
// locking.
namespace ui::i18n {

// Activates the best-matching embedded language for `preferred_locale`
// (a BCP-47-ish tag, e.g. "zh-CN", "en-US"). Pass nullptr (the default) to
// follow the OS-reported user locale instead (see DetectSystemLocale()).
// Falls back to "en" when nothing matches; call once before the first frame.
void Initialize(const char* preferred_locale = nullptr);

// Translates `key` in the currently active language. Falls back to the "en"
// pack if the active language has no entry for `key`, then to `key` itself
// if even "en" is missing it -- this also makes an un-translated key easy to
// spot on screen. Never returns nullptr.
const char* Tr(const char* key);

// Tr(key) with a "###stable_id" suffix appended. imgui_md2 widgets that
// derive their ImGuiID from the label they're given (Switch, ContainedButton,
// ...) truncate the *visible* text at "##" but hash the *whole* string for
// the ID, so appending "###stable_id" keeps a widget's identity/state intact
// across a language switch even though its displayed text changes.
// `stable_id` must be the same every call for a given widget.
std::string TrLabel(const char* key, const char* stable_id);

// The BCP-47-ish code of the language currently active (e.g. "en", "zh-CN").
const char* CurrentLanguageCode();

// Enumeration of embedded languages, e.g. for building a manual language
// switcher. `i` ranges over [0, LanguageCount()).
int LanguageCount();
const char* LanguageDisplayName(int i); // the language's own name for itself, e.g. "简体中文"
const char* LanguageCode(int i);        // e.g. "zh-CN"
int CurrentLanguageIndex();
void SetLanguageIndex(int i); // out-of-range i is ignored

// Reads the OS-reported user locale as a BCP-47-ish string (e.g. "zh-CN"),
// independent of which languages are actually embedded. Windows:
// GetUserDefaultLocaleName. POSIX: the first non-empty of LC_ALL,
// LC_MESSAGES, LANG, truncated before a trailing '.' (encoding) or '@'
// (modifier). Returns an empty string if nothing could be determined.
std::string DetectSystemLocale();

} // namespace ui::i18n
