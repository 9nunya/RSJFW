# Changelog

## [2.0.0] - 2024-12-24

### 🎄 The Christmas Release - RSJFW Goes Solo

RSJFW is now completely independent. No Vinegar code, no Vinegar dependencies, just pure RSJFW supremacy.

### Added

#### GUI & UX
- **GPU Selection Tab** - Choose which GPU to use for Studio with automatic `lspci` detection and `DRI_PRIME` support
- **Progress-based Reinstall** - New `rsjfw reinstall` command with a dedicated progress GUI for downloads and prefix setup
- **Native File Picker** - Uses `xdg-desktop-portal` to open your system's native file manager (Nautilus, Dolphin, etc.)
- **Modern hierarchical page system** - Home, Settings, and Troubleshooting pages with smooth animations
- **Two-phase tab animations** - Fade out + slide, then slide + fade in for buttery smooth transitions  
- **Full-width crimson navigation bar** - Borderless, polished, no black gaps
- **Home page quick actions** - Launch/Kill Studio, Open Prefix, winecfg, Kill Wine, View Logs, Refresh
- **Troubleshooting tab** - System info, log viewer, Wine management tools
- **FFlag presets** - One-click FPS unlock, lighting tweaks, DPI scaling fixes

#### Infrastructure
- **Wine Logging Integration** - Wine output is now piped directly into the main RSJFW log file for easier debugging
- **Single Instance Listener** - Config GUI now listens for protocol links at all times; clicking a link will now work even if the config is open
- **Wine/Proton module** (`wine.cpp`) - Clean abstraction for Wine prefix management
- **DXVK module** (`dxvk.cpp`) - Automatic DXVK download and installation from multiple sources (Sarek, async-Sarek, or custom)
- **Page header organization** - Headers moved to `include/rsjfw/pages/`
- **AppImage builds** - Portable distribution that works on any Linux distro
- **GitHub Actions workflow** - Builds both AppImage and Arch tarball on push

#### Protocol Handling
- **Robust Protocol Forwarding** - Fixed cases where protocol links wouldn't open if RSJFW was already running
- **Automatic protocol registration** - `roblox-studio://` and `roblox-studio-auth://` URLs handled natively
- **Proper desktop file** - Compliant with freedesktop spec

### Changed
- **Vulkan layer completely rewritten** - 100% original code, `RsjfwLayer_` prefix, `rsjfw` namespace, 165 lines vs Vinegar's 307
- **Simplified main.cpp** - Removed all inline comments, cleaner code
- **Launcher binary detection** - Uses `/proc/self/exe` to find itself, works from any location
- **Studio process detection** - Uses `pgrep` with proper grep exclusion

### Removed
- **All WebView2 code** - Completely exterminated, no traces remain
- **Vinegar references** - We're independent now
- **Inline comments** - Production-ready clean code
- **Old AUR workflow** - Replaced with unified build workflow

### Fixed
- **Version Selector Populating** - Fixed bug where DXVK/Wine versions wouldn't show up in dropdowns automatically
- **Desktop File Protocol Fix** - Fixed `.desktop` file invalidity that prevented some environments from launching links
- **Launch from config** - Now uses same binary path, doesn't spawn wrong version
- **Settings sidebar animations** - Now has same two-phase animation as main tabs
- **Navigation bar styling** - Full crimson background, no black gaps at edges

---

*RSJFW - Because Vinegar is garbage and we can do better.*
