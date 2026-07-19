/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "mc_download.h"
#include "http.h"
#include "mc_version.h"
#include "sha1.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>

using json = nlohmann::json;
namespace fs = util::fs;

namespace {

RemoteVersionType parse_type(const std::string& t)
{
    if (t == "release")
        return RemoteVersionType::Release;
    if (t == "snapshot")
        return RemoteVersionType::Snapshot;
    if (t == "old_beta")
        return RemoteVersionType::OldBeta;
    if (t == "old_alpha")
        return RemoteVersionType::OldAlpha;
    return RemoteVersionType::Other;
}

bool rules_ok(const json& lib)
{
    if (!lib.contains("rules"))
        return true;
    return mc_rules_allow(lib["rules"]);
}

std::string artifact_url(const json& art)
{
    if (art.contains("url"))
        return art["url"].get<std::string>();
    return {};
}

} // namespace

McDownloadService::~McDownloadService()
{
    request_cancel();
    if (thread_.joinable())
        thread_.join();
}

std::string McDownloadService::mirror_url(DownloadMirror m, const std::string& original_url)
{
    if (m == DownloadMirror::Official || original_url.empty())
        return original_url;

    // BMCLAPI rewrite (common launcher convention)
    std::string u = original_url;
    auto rep = [&](const char* from, const char* to) {
        u = util::replace_all(u, from, to);
    };
    rep("https://launcher.mojang.com", "https://bmclapi2.bangbang93.com");
    rep("https://piston-meta.mojang.com", "https://bmclapi2.bangbang93.com");
    rep("https://piston-data.mojang.com", "https://bmclapi2.bangbang93.com");
    rep("https://launchermeta.mojang.com", "https://bmclapi2.bangbang93.com");
    rep("https://libraries.minecraft.net", "https://bmclapi2.bangbang93.com/maven");
    rep("https://resources.download.minecraft.net", "https://bmclapi2.bangbang93.com/assets");
    rep("https://maven.minecraftforge.net", "https://bmclapi2.bangbang93.com/maven");
    rep("https://maven.fabricmc.net", "https://bmclapi2.bangbang93.com/maven");
    return u;
}

void McDownloadService::set_status(const DownloadStatus& s)
{
    std::lock_guard<std::mutex> lock(mu_);
    status_ = s;
}

DownloadStatus McDownloadService::snapshot() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

bool McDownloadService::should_cancel() const
{
    return cancel_.load();
}

void McDownloadService::request_cancel()
{
    cancel_.store(true);
}

bool McDownloadService::fetch_manifest(DownloadMirror mirror, std::string* err)
{
    const char* official = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
    std::string url = mirror_url(mirror, official);
    std::string body = http::get_string(url, err, 60000);
    if (body.empty())
        return false;

    try {
        json j = json::parse(body);
        remote_versions_.clear();
        if (j.contains("latest")) {
            latest_release_ = j["latest"].value("release", "");
            latest_snapshot_ = j["latest"].value("snapshot", "");
        }
        if (j.contains("versions") && j["versions"].is_array()) {
            for (const auto& v : j["versions"]) {
                RemoteVersion rv;
                rv.id = v.value("id", "");
                rv.type = v.value("type", "");
                rv.url = v.value("url", "");
                rv.sha1 = v.value("sha1", "");
                rv.type_enum = parse_type(rv.type);
                if (!rv.id.empty() && !rv.url.empty())
                    remote_versions_.push_back(std::move(rv));
            }
        }
        return !remote_versions_.empty();
    } catch (const std::exception& e) {
        if (err)
            *err = std::string("解析 version_manifest 失败: ") + e.what();
        return false;
    }
}

bool McDownloadService::start_install(const std::string& game_dir,
                                      const std::string& version_id,
                                      DownloadMirror mirror)
{
    if (busy_.load())
        return false;
    if (thread_.joinable())
        thread_.join();

    cancel_.store(false);
    busy_.store(true);
    DownloadStatus st;
    st.busy = true;
    st.phase = "prepare";
    st.message = "准备安装 " + version_id;
    set_status(st);

    thread_ = std::thread(&McDownloadService::worker, this, game_dir, version_id, mirror);
    return true;
}

void McDownloadService::worker(std::string game_dir, std::string version_id, DownloadMirror mirror)
{
    DownloadStatus st;
    st.busy = true;

    auto fail = [&](const std::string& msg) {
        st.busy = false;
        st.error = msg;
        st.message = msg;
        set_status(st);
        busy_.store(false);
    };

    auto find_remote = [&]() -> const RemoteVersion* {
        for (const auto& v : remote_versions_)
            if (v.id == version_id)
                return &v;
        return nullptr;
    };

    // Ensure manifest loaded
    if (remote_versions_.empty()) {
        st.phase = "manifest";
        st.message = "获取版本列表…";
        set_status(st);
        std::string err;
        if (!fetch_manifest(mirror, &err)) {
            fail(err.empty() ? "获取版本列表失败" : err);
            return;
        }
    }

    const RemoteVersion* remote = find_remote();
    if (!remote) {
        fail("远程列表中无此版本: " + version_id);
        return;
    }

    fs::path gdir(game_dir);
    fs::path vdir = gdir / "versions" / version_id;
    fs::path vjson = vdir / (version_id + ".json");
    fs::path vjar = vdir / (version_id + ".jar");

    // 1) version json
    st.phase = "version.json";
    st.message = "下载版本元数据…";
    st.current_file = version_id + ".json";
    st.files_done = 0;
    st.files_total = 1;
    st.overall_progress = 0.02f;
    set_status(st);

    if (should_cancel()) {
        fail("已取消");
        return;
    }

    std::string err;
    std::string meta_url = mirror_url(mirror, remote->url);
    if (!http::download_file(meta_url, vjson, remote->sha1, &err)) {
        fail(err.empty() ? "下载 version.json 失败" : err);
        return;
    }

    json version;
    try {
        version = json::parse(util::read_text_file(vjson));
    } catch (const std::exception& e) {
        fail(std::string("解析 version.json 失败: ") + e.what());
        return;
    }

    // Collect download jobs
    struct Job {
        std::string url;
        fs::path path;
        std::string sha1;
        std::string label;
    };
    std::vector<Job> jobs;

    // client jar
    if (version.contains("downloads") && version["downloads"].contains("client")) {
        const auto& c = version["downloads"]["client"];
        Job j;
        j.url = mirror_url(mirror, c.value("url", ""));
        j.sha1 = c.value("sha1", "");
        j.path = vjar;
        j.label = version_id + ".jar";
        if (!j.url.empty())
            jobs.push_back(std::move(j));
    }

    // libraries
    if (version.contains("libraries") && version["libraries"].is_array()) {
        for (const auto& lib : version["libraries"]) {
            if (!rules_ok(lib))
                continue;
            if (lib.contains("downloads")) {
                const auto& dl = lib["downloads"];
                if (dl.contains("artifact")) {
                    const auto& art = dl["artifact"];
                    std::string path = art.value("path", "");
                    if (!path.empty()) {
                        Job j;
                        j.url = mirror_url(mirror, artifact_url(art));
                        if (j.url.empty() && art.contains("path")) {
                            // fallback libraries.minecraft.net
                            j.url = mirror_url(mirror, "https://libraries.minecraft.net/" + path);
                        }
                        j.sha1 = art.value("sha1", "");
                        j.path = gdir / "libraries" / path;
                        j.label = fs::path(path).filename().string();
                        if (!j.url.empty())
                            jobs.push_back(std::move(j));
                    }
                }
                if (dl.contains("classifiers") && lib.contains("natives")) {
                    std::string classifier;
                    if (lib["natives"].contains("windows")) {
                        classifier = lib["natives"]["windows"].get<std::string>();
#if defined(_WIN64) || defined(_M_X64)
                        classifier = util::replace_all(classifier, "${arch}", "64");
#else
                        classifier = util::replace_all(classifier, "${arch}", "32");
#endif
                    }
                    if (!classifier.empty() && dl["classifiers"].contains(classifier)) {
                        const auto& art = dl["classifiers"][classifier];
                        std::string path = art.value("path", "");
                        if (!path.empty()) {
                            Job j;
                            j.url = mirror_url(mirror, artifact_url(art));
                            if (j.url.empty())
                                j.url = mirror_url(mirror, "https://libraries.minecraft.net/" + path);
                            j.sha1 = art.value("sha1", "");
                            j.path = gdir / "libraries" / path;
                            j.label = fs::path(path).filename().string();
                            jobs.push_back(std::move(j));
                        }
                    }
                }
            }
        }
    }

    // asset index
    std::string asset_index_id;
    json asset_index_obj;
    if (version.contains("assetIndex")) {
        asset_index_obj = version["assetIndex"];
        asset_index_id = asset_index_obj.value("id", "");
        std::string aurl = mirror_url(mirror, asset_index_obj.value("url", ""));
        std::string asha = asset_index_obj.value("sha1", "");
        if (!asset_index_id.empty() && !aurl.empty()) {
            Job j;
            j.url = aurl;
            j.sha1 = asha;
            j.path = gdir / "assets" / "indexes" / (asset_index_id + ".json");
            j.label = "assets/indexes/" + asset_index_id + ".json";
            jobs.push_back(std::move(j));
        }
    }

    st.phase = "libraries";
    st.files_total = (int)jobs.size();
    st.files_done = 0;
    st.message = "下载依赖与客户端…";
    set_status(st);

    for (size_t i = 0; i < jobs.size(); ++i) {
        if (should_cancel()) {
            fail("已取消");
            return;
        }
        const auto& job = jobs[i];
        st.current_file = job.label;
        st.files_done = (int)i;
        st.overall_progress = 0.05f + 0.55f * (float)i / std::max<size_t>(1, jobs.size());
        st.message = "下载 " + job.label;
        set_status(st);

        err.clear();
        if (!http::download_file(job.url, job.path, job.sha1, &err, [&](const http::DownloadProgress& p) {
                DownloadStatus s = st;
                if (p.total > 0)
                    s.message = job.label + "  " + std::to_string(p.downloaded / 1024) + " / " +
                                std::to_string(p.total / 1024) + " KB";
                set_status(s);
            })) {
            fail(err.empty() ? ("下载失败: " + job.label) : err);
            return;
        }
    }
    st.files_done = (int)jobs.size();

    // assets objects
    if (!asset_index_id.empty()) {
        fs::path index_path = gdir / "assets" / "indexes" / (asset_index_id + ".json");
        json index;
        try {
            index = json::parse(util::read_text_file(index_path));
        } catch (const std::exception& e) {
            fail(std::string("解析 asset index 失败: ") + e.what());
            return;
        }

        std::vector<Job> assets;
        if (index.contains("objects") && index["objects"].is_object()) {
            for (auto it = index["objects"].begin(); it != index["objects"].end(); ++it) {
                std::string hash = it.value().value("hash", "");
                if (hash.size() < 2)
                    continue;
                Job j;
                j.sha1 = hash;
                j.path = gdir / "assets" / "objects" / hash.substr(0, 2) / hash;
                j.label = it.key();
                // resources.download.minecraft.net/<2>/<hash>
                j.url = mirror_url(mirror, "https://resources.download.minecraft.net/" + hash.substr(0, 2) + "/" + hash);
                assets.push_back(std::move(j));
            }
        }

        st.phase = "assets";
        st.files_total = (int)assets.size();
        st.files_done = 0;
        st.message = "下载游戏资源 (" + std::to_string(assets.size()) + " 个文件)…";
        set_status(st);

        for (size_t i = 0; i < assets.size(); ++i) {
            if (should_cancel()) {
                fail("已取消");
                return;
            }
            const auto& job = assets[i];
            // skip exists with correct hash quickly
            if (util::sha1_equals_file(job.path, job.sha1)) {
                st.files_done = (int)i + 1;
                st.overall_progress = 0.60f + 0.38f * (float)(i + 1) / std::max<size_t>(1, assets.size());
                if (i % 50 == 0) {
                    st.current_file = job.label;
                    st.message = "校验/下载资源 " + std::to_string(i + 1) + "/" + std::to_string(assets.size());
                    set_status(st);
                }
                continue;
            }

            st.current_file = job.label;
            st.files_done = (int)i;
            st.overall_progress = 0.60f + 0.38f * (float)i / std::max<size_t>(1, assets.size());
            st.message = "资源 " + std::to_string(i + 1) + "/" + std::to_string(assets.size());
            set_status(st);

            err.clear();
            if (!http::download_file(job.url, job.path, job.sha1, &err)) {
                fail(err.empty() ? ("资源下载失败: " + job.label) : err);
                return;
            }
        }
        st.files_done = (int)assets.size();
    }

    // logging config optional
    if (version.contains("logging") && version["logging"].contains("client") &&
        version["logging"]["client"].contains("file")) {
        const auto& f = version["logging"]["client"]["file"];
        std::string id = f.value("id", "client-1.12.xml");
        std::string url = mirror_url(mirror, f.value("url", ""));
        std::string sha = f.value("sha1", "");
        if (!url.empty()) {
            fs::path p = gdir / "assets" / "log_configs" / id;
            http::download_file(url, p, sha, nullptr);
        }
    }

    st.busy = false;
    st.overall_progress = 1.f;
    st.phase = "done";
    st.current_file.clear();
    st.message = "安装完成: " + version_id;
    st.error.clear();
    set_status(st);
    busy_.store(false);
}
