#pragma once

// Settings > Download: mirror-source selection (Auto/official-only/BMCLAPI-
// only) and the two download-parallelism knobs (concurrency = simultaneous
// files, threads = HTTP Range segments per large file). Mirrors
// ui/java_settings.h being a separate small module rather than growing
// frame.cpp further. Persistence goes through config::Config's flat scalar
// store (download.mirror/download.concurrency/download.threads) -- write on
// change, same as every other settings section.
//
// Threading: render-thread-exclusive, same as config::Config / ui::theme /
// ui::i18n -- BuildDownloadSettings() is only called from inside
// ui::BuildFrame().
namespace ui {

// Renders the Settings > Download section body. Called from frame.cpp's
// SettingsSection::Download case.
void BuildDownloadSettings();

} // namespace ui
