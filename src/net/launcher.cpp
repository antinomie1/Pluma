#include "net/launcher.h"

#include "net/manifest.h"

#include <miniz.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace net {
namespace {

namespace fs = std::filesystem;

// Classpath entry separator: ';' on Windows, ':' elsewhere -- the JVM's own
// platform convention, unrelated to the filesystem separator.
#ifdef _WIN32
constexpr char kClasspathSep = ';';
#else
constexpr char kClasspathSep = ':';
#endif

std::string ReadWholeFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::string();
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool EndsWithIgnoreCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// True for the shared-library payloads inside a native jar we actually want on
// java.library.path -- everything else (META-INF, checksums, module-info) is
// skipped.
bool IsNativePayload(const std::string& name) {
    return EndsWithIgnoreCase(name, ".dll") || EndsWithIgnoreCase(name, ".so") ||
           EndsWithIgnoreCase(name, ".dylib") || EndsWithIgnoreCase(name, ".jnilib");
}

// Extracts the native .dll/.so/.dylib payloads from one native jar into
// `natives_dir` (flattened to their basename). Best-effort: a jar that fails
// to open is skipped rather than failing the whole launch, matching how the
// download pipeline treats best-effort artifacts.
void ExtractNatives(const std::string& jar_path, const fs::path& natives_dir) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, jar_path.c_str(), 0)) return;

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        const std::string entry_name = st.m_filename;
        if (!IsNativePayload(entry_name)) continue;

        const std::string base = fs::path(entry_name).filename().string();
        const fs::path dest = natives_dir / base;
        mz_zip_reader_extract_to_file(&zip, i, dest.string().c_str(), 0);
    }

    mz_zip_reader_end(&zip);
}

// Splits a space-separated user JVM-argument string into individual argv
// entries. Quoting is deliberately not supported here (JVM args like -Xmx or
// -D flags don't need it); a value with a space would need to be a single
// field, which this simple split doesn't handle -- acceptable for the flags
// this field is meant for.
std::vector<std::string> SplitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token) out.push_back(token);
    return out;
}

// Replaces every occurrence of each ${placeholder} in `arg` with its resolved
// value. Called per game-argument token.
std::string Substitute(std::string arg, const std::vector<std::pair<std::string, std::string>>& vars) {
    for (const auto& [key, value] : vars) {
        std::size_t pos = 0;
        while ((pos = arg.find(key, pos)) != std::string::npos) {
            arg.replace(pos, key.size(), value);
            pos += value.size();
        }
    }
    return arg;
}

} // namespace

LaunchCommand PrepareLaunch(const LaunchParams& params) {
    LaunchCommand cmd;
    cmd.exe = params.java_exe;

    const fs::path game_dir(params.game_dir);
    const fs::path version_dir = game_dir / "versions" / params.instance_name;
    const fs::path json_path = version_dir / (params.instance_name + ".json");
    const fs::path client_jar = version_dir / (params.instance_name + ".jar");

    // Per-instance isolation (params.isolate_instance, default on): the game
    // directory (Minecraft's ${game_directory} / --gameDir, and the process
    // working directory) is the instance's own versions/<name>/ folder rather
    // than the shared .minecraft root -- so mods, saves, resourcepacks,
    // shaderpacks, options.txt, logs and screenshots all live under this
    // instance and never bleed between instances. With isolation off, the
    // shared root is used and that user data is shared across instances. Either
    // way libraries/ and assets/ (deduplicated by Maven coordinate / content
    // hash) stay shared at the root; those are version content, not user data.
    const fs::path instance_game_dir = params.isolate_instance ? version_dir : game_dir;
    cmd.cwd = instance_game_dir.string();

    std::error_code ec;
    if (!fs::exists(json_path, ec)) {
        cmd.error = "version json not found: " + json_path.string();
        return cmd;
    }
    if (!fs::exists(client_jar, ec)) {
        cmd.error = "client jar not found: " + client_jar.string();
        return cmd;
    }

    const std::string json = ReadWholeFile(json_path);
    if (json.empty()) {
        cmd.error = "failed to read version json";
        return cmd;
    }
    const LaunchProfile profile = ParseLaunchProfile(json);
    if (profile.main_class.empty()) {
        cmd.error = "version json has no mainClass";
        return cmd;
    }

    // Extract natives into <version_dir>/natives/.
    const fs::path natives_dir = version_dir / "natives";
    fs::create_directories(natives_dir, ec);
    for (const std::string& rel : profile.native_libs) {
        const fs::path jar = game_dir / "libraries" / rel;
        if (fs::exists(jar, ec)) ExtractNatives(jar.string(), natives_dir);
    }

    // Classpath: every allowed library, then the client jar itself.
    std::string classpath;
    for (const std::string& rel : profile.classpath_libs) {
        if (!classpath.empty()) classpath.push_back(kClasspathSep);
        classpath += (game_dir / "libraries" / rel).string();
    }
    if (!classpath.empty()) classpath.push_back(kClasspathSep);
    classpath += client_jar.string();

    // JVM arguments (constructed here rather than parsing arguments.jvm, which
    // is version-specific and rule-gated -- this minimal set works across
    // versions).
    if (params.memory_mb > 0) {
        cmd.args.push_back("-Xmx" + std::to_string(params.memory_mb) + "M");
    }
    for (std::string& a : SplitArgs(params.jvm_args)) cmd.args.push_back(std::move(a));
    cmd.args.push_back("-Djava.library.path=" + natives_dir.string());
    cmd.args.push_back("-cp");
    cmd.args.push_back(classpath);
    cmd.args.push_back(profile.main_class);

    // Game arguments, with placeholder substitution. game_directory follows
    // the isolation choice above (see cmd.cwd); assets stay shared regardless.
    const std::string assets_dir = (game_dir / "assets").string();
    const std::string game_directory = instance_game_dir.string();
    const std::vector<std::pair<std::string, std::string>> vars = {
        {"${auth_player_name}", params.player_name},
        {"${version_name}", params.instance_name},
        {"${game_directory}", game_directory},
        {"${assets_root}", assets_dir},
        {"${game_assets}", assets_dir},
        {"${assets_index_name}", profile.asset_index_id},
        {"${auth_uuid}", params.player_uuid},
        {"${auth_access_token}", "0"},
        {"${clientid}", ""},
        {"${auth_xuid}", ""},
        {"${user_type}", "legacy"},
        {"${version_type}", profile.type},
        {"${user_properties}", "{}"},
    };
    for (const std::string& raw : profile.game_args) {
        cmd.args.push_back(Substitute(raw, vars));
    }

    return cmd;
}

} // namespace net
