#include "net/mirror.h"

#include <utility>

namespace net {

std::string ToBmclapiUrl(const std::string& official_url) {
    // Special-cased full-prefix rewrites: these two live under a different
    // path root on BMCLAPI, not just a different host.
    static const std::pair<const char*, const char*> kPrefixRewrites[] = {
        {"https://libraries.minecraft.net", "https://bmclapi2.bangbang93.com/maven"},
        {"https://resources.download.minecraft.net", "https://bmclapi2.bangbang93.com/assets"},
    };
    for (const auto& [from, to] : kPrefixRewrites) {
        const std::string prefix(from);
        if (official_url.compare(0, prefix.size(), prefix) == 0) {
            return std::string(to) + official_url.substr(prefix.size());
        }
    }

    // Plain host substitution for the piston-meta/piston-data/launcher/
    // launchermeta family -- same path, only the host changes.
    static const char* const kMojangHosts[] = {
        "piston-meta.mojang.com",
        "piston-data.mojang.com",
        "launcher.mojang.com",
        "launchermeta.mojang.com",
    };
    const std::size_t scheme_end = official_url.find("://");
    if (scheme_end == std::string::npos) return official_url;
    const std::size_t host_start = scheme_end + 3;
    const std::size_t host_end = official_url.find('/', host_start);
    const std::string host = official_url.substr(
        host_start, host_end == std::string::npos ? std::string::npos : host_end - host_start);

    for (const char* mojang_host : kMojangHosts) {
        if (host == mojang_host) {
            const std::string rest =
                host_end == std::string::npos ? std::string() : official_url.substr(host_end);
            return official_url.substr(0, host_start) + "bmclapi2.bangbang93.com" + rest;
        }
    }

    return official_url; // no known rewrite -- pass through unchanged
}

std::vector<std::string> CandidateUrls(const std::string& official_url, MirrorMode mode) {
    switch (mode) {
        case MirrorMode::OfficialOnly:
            return {official_url};
        case MirrorMode::BmclapiOnly:
            return {ToBmclapiUrl(official_url)};
        case MirrorMode::Auto:
        default:
            return {ToBmclapiUrl(official_url), official_url};
    }
}

} // namespace net
