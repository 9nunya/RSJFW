# RSJFW

<img src="assets/logo.png" width="150" align="left" style="margin-right: 20px;">

&nbsp;

**Roblox Studio Just Fucking Works.** 

Because other launchers like to play games with "experimental" features and broken Go runtimes. 

Vinegar K till my bones decay, RSJFW SUPREMACY!!!

<br clear="left"/>

### Installation

**Arch Linux (AUR)**
```bash
# Clone and build package
makepkg -si
```

**Ubuntu / Debian**
```bash
# Build DEB package
mkdir build && cd build
cmake ..
cpack -G DEB
sudo dpkg -i rsjfw-*.deb
```

**Arch Linux (AUR)**
```bash
# Clone and build package
makepkg -si
```

**Ubuntu / Debian**
```bash
# Build DEB package
mkdir build && cd build
cmake ..
cpack -G DEB
sudo dpkg -i rsjfw-*.deb
```

**Manual Install (Dev)**
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./rsjfw install
```

### Usage
- `rsjfw install` - Get Studio.
- `rsjfw launch` - Open Studio.
- `rsjfw kill` - Nuke it.

If you click "Login with Browser", it'll just work once you setup the protocol handler.

---
*Built with spite and C++.*
