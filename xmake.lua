set_project("pluma")
set_languages("cxx17")

add_rules("mode.debug", "mode.release")

if is_os("windows") then
    add_defines("NOMINMAX")
end
-- CJK system-font discovery (src/platform/system_fonts.cpp) uses fontconfig
-- on Linux to resolve a font without hard-coding a name or path.
if is_os("linux") then
    add_requires("fontconfig")
end

add_requires("simdjson")
add_requires("glfw")
-- Pin to the version imgui-md2 targets so both link the same ImGui ABI.
add_requires("imgui v1.92.8", {configs = {glfw = true, opengl3 = true, shared = false}})

-- Embed Roboto Medium and Bold (in addition to Regular and Material Icons):
-- Medium backs the app bar / button text, and Bold backs the real-bold bottom
-- nav buttons (merged with a runtime bold CJK face). Light is not embedded;
-- text at Light weight falls back to Regular at runtime.
set_config("imgui_md2_embed_medium", true)
set_config("imgui_md2_embed_bold", true)

includes("imgui_md2")

-- Shared vocabulary types and thread-safe primitives. No third-party
-- dependencies; every other module depends on this one.
target("core")
    set_kind("static")
    add_files("src/core/*.cpp")
    add_headerfiles("src/(core/*.h)")
    add_includedirs("src", {public = true})

-- GLFW window/context ownership. The only module allowed to touch GLFW.
target("platform")
    set_kind("static")
    add_files("src/platform/*.cpp")
    add_headerfiles("src/(platform/*.h)")
    add_deps("core")
    add_packages("glfw")
    if is_os("windows") then
        add_syslinks("dwmapi", "user32", "dwrite")
    elseif is_os("linux") then
        add_packages("fontconfig")
    end

-- Simulation/business logic, ticking on its own thread.
target("logic")
    set_kind("static")
    add_files("src/logic/*.cpp")
    add_headerfiles("src/(logic/*.h)")
    add_deps("core")

-- simdjson-backed flat JSON config read/write (src/config). simdjson is
-- read-only, so writes go through a hand-written serializer; see
-- src/config/config.h for the render-thread-exclusive, no-lock convention
-- shared with ui::theme/ui::i18n.
target("config")
    set_kind("static")
    add_files("src/config/*.cpp")
    add_headerfiles("src/(config/*.h)")
    add_deps("core")
    add_packages("simdjson")

-- Pure ImGui/imgui-md2 widget code. No GL calls; depends on platform only
-- for Window's move/minimize/close affordances (the custom title bar).
target("ui")
    set_kind("static")
    add_files("src/ui/*.cpp")
    add_headerfiles("src/(ui/*.h)")
    add_deps("core", "platform", "config", "imgui-md2")
    add_packages("imgui")

-- ImGui context + GLFW/OpenGL3 backends; orchestrates one frame.
target("render")
    set_kind("static")
    add_files("src/render/*.cpp")
    add_headerfiles("src/(render/*.h)")
    add_deps("core", "platform", "logic", "ui", "config", "imgui-md2")
    add_packages("imgui", "glfw")
    if is_os("windows") then
        add_syslinks("opengl32")
    else
        add_syslinks("GL")
    end

target("pluma")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("src")
    add_deps("core", "platform", "logic", "ui", "render", "config", "imgui-md2")
    add_packages("simdjson", "glfw", "imgui")
    if is_os("windows") then
        add_syslinks("dwmapi", "user32", "opengl32")
    else
        add_syslinks("GL")
    end
