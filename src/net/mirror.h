#pragma once

// BMCLAPI mirror URL rewriting, ported from PCL-CE's host/prefix-substitution
// rules (path segments are kept as-is -- only the scheme+host, or in the
// libraries/resources cases the whole host+leading path, changes).
#include "net/types.h"

#include <string>
#include <vector>

namespace net {

// Rewrites an official Mojang/libraries/resources URL to its BMCLAPI mirror
// equivalent. Returns `official_url` unchanged if it doesn't match any of the
// known official hosts (nothing to rewrite).
std::string ToBmclapiUrl(const std::string& official_url);

// Builds an ordered list of candidate URLs to try for `official_url`,
// honoring `mode`: Auto = [mirror, official] (mirror first, official
// fallback), OfficialOnly = [official], BmclapiOnly = [mirror].
std::vector<std::string> CandidateUrls(const std::string& official_url, MirrorMode mode);

} // namespace net
