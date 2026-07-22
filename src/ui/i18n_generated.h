#pragma once

// Stable, hand-written header for the generated language-table translation
// unit (src/ui/i18n_generated.cpp), produced by
// scripts/generate_embedded_languages.ps1 from assets/lang/*.xml. Declared
// unconditionally so this header never needs regenerating alongside the .cpp
// -- mirrors imgui_md2/src/embedded_fonts.h's role for the font blobs.
namespace ui::i18n_gen {

// Number of distinct translation keys, and the key at index `i` (ascending,
// stable order -- callers may binary-search it). Returns nullptr if `i` is
// out of range.
int KeyCount();
const char* Key(int i);

// Number of embedded languages, and the always-present index-based accessors
// for each language's BCP-47-ish code (e.g. "en", "zh-CN") and its own
// display name (e.g. "English", "简体中文"). Returns nullptr if `lang` is out
// of range.
int LanguageCount();
const char* LangCode(int lang);
const char* LangName(int lang);

// The UTF-8 value for `lang`'s translation of Key(key_index), or nullptr if
// that language has no entry for the key (or either index is out of range).
const char* Value(int lang, int key_index);

} // namespace ui::i18n_gen
