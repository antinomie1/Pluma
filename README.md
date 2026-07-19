# Minecraft Minimal Launcher

基于 Dear ImGui + GLFW + OpenGL 的 **最小可玩** Minecraft 启动器（Windows / 离线模式）。

## 能做什么

- 扫描本地 `.minecraft/versions` 中已安装版本
- **从 Mojang / BMCLAPI 下载并安装** 原版（client + libraries + assets，SHA1 校验）
- 自动/手动查找 Java（含常见 JDK 与官方启动器 runtime）
- 解析 version JSON（含 `inheritsFrom` 合并）
- 组装 classpath、解压 natives、离线 UUID
- 离线启动游戏

## 尚未实现

- 微软正版登录
- Forge / Fabric / Quilt / NeoForge 安装
- Mod / 整合包市场
- 多实例档案、皮肤管理等

## 构建

依赖：

- CMake ≥ 3.16
- C++17 编译器（MinGW-w64 / llvm-mingw 可链接仓库内 `lib/libglfw3.a`）
- Windows（`opengl32` / `shell32` / `winhttp` / `bcrypt`）

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

可执行文件：`build/launcher.exe`

## 使用

1. 运行 `launcher.exe`
2. **下载**：获取版本列表 → 选择版本 → 安装
3. **档案管理**：确认游戏目录（默认 `%APPDATA%\.minecraft`）
4. **设置**：检测或填写 `javaw.exe`
5. **主页**：选择版本 → 填写离线用户名 → **启动游戏**

配置保存在：`%APPDATA%\mc-minimal-launcher\config.json`

## 许可

本项目以 [GNU GPL v2.0 only](LICENSE) 授权。

第三方组件保留各自许可证（Dear ImGui MIT、GLFW zlib/libpng、nlohmann/json MIT 等）。
