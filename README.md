# RSJFW

<img src="assets/logo.png" width="150" align="left" style="margin-right: 20px;">

&nbsp;

**Roblox Studio Just Fucking Works.** 

no configuration errors, no launching then crashing, no dependency issues, no shitty env var setting

it JUST fucking works. **Vinegar K till my bones decay, RSJFW SUPREMACY!!!**

<br clear="left"/>

> [!WARNING]
> I dont need to abide by your shitty guidelines. You are getting in the way of progress.
> [Discord Server](https://discord.gg/7EMN6v7G)

> [!WARNING]
> RSJFW IS LITERALLY BRAND NEW, EXPECT ISSUES! IT JUST FUCKING WORKS, BUT WHO KNOWS. BUGS WILL ALWAYS EXIST.

## Why RSJFW?

Because Vinegar is hot garbage. Let me explain:

- **3 days vs 2+ years** - I built a working Roblox Studio launcher in 3 days. They took over 2 years to reach a "stable" state that still doesn't fucking work properly.
- **Actually fixing issues** - I respond to issues and FIX THEM. Vinegar maintainers just tell you to "try this workaround" and leave tickets open for months.
- **Fully from scratch** - They stalked my DevForum post to hate on it. They harassed me in GitHub issues. They got petty about their Vulkan layer code. Fine. RSJFW is on its own now. We don't need ANYTHING from them.
- **100% original code** - "you skidded code meh meh meh" okay, what about now fuckhead? go and stalk my shit now, see if you can find anything to report.

*To the Vinegar team:* keep stalking me sweaty fucking losers. i yearn for your tears. if i had to make an entire separate package to get you to actually fix your shit, that is fucking hilarious.
you wanna report me and mald? go ahead LOL my shit works better than your stupid fucking go package ever will. you mad im insulting you in a readme? do something about it see if i care.
and if you have something to say, pipe down. you may only talk if my software is buggier than yours. then i will admit defeat.
---

## Installation

**Arch Linux (AUR)**
```bash
yay -S rsjfw # works now i think PLEASE scream at me if it doesn't
# or
yay -S rsjfw-git
```

**AppImage (Works on any distro)**
Download the latest `.AppImage` from [Releases](https://github.com/9nunya/RSJFW/releases), make it executable, and run:
```bash
chmod +x RSJFW*.AppImage
./RSJFW*.AppImage config
```

**Manual Build**
```bash
git clone --recursive https://github.com/9nunya/RSJFW
cd RSJFW
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cd build && make -j$(nproc)
```

## Features

- **One-click launch** - Just run `rsjfw launch` and it handles everything
- **Wine/Proton support** - Use system Wine or custom Wine/Proton GE builds
- **DXVK management** - Automatic DXVK download and installation
- **FFlag presets** - Built-in graphics tweaks and FPS unlock
- **Troubleshooting tools** - Built-in diagnostics, log viewer, and Wine management

## Usage

| Command | Description |
|---------|-------------|
| `rsjfw launch` | Launch or install Roblox Studio |
| `rsjfw config` | Open the configuration editor |
| `rsjfw install` | Install/update Roblox Studio without launching |
| `rsjfw kill` | Kill any running Roblox Studio instances |
| `rsjfw help` | Show help |

Just click links on roblox.com and RSJFW handles the rest.

## Contributing

Unlike SOME projects, contributions are actually welcome here. Open an issue, submit a PR, whatever. I'll actually respond.
Don't try to fucking silence me though. RSJFW will ALWAYS be Roblox Studio Just Fucking Works. Go and cry about it.

*built with the SUPERIOR language, C++, fuck go*
