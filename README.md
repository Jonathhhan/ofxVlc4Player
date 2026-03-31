Inspired by [ofxVLCVideoPlayer](https://github.com/jnakanojp/ofxVLCVideoPlayer).

`ofxVlc4Player` binds libVLC 4 for openFrameworks.

## Installing libVLC

The addon ships with helper scripts that download libVLC into the addon-local `libs/libvlc` layout.

### Windows

Run:

```powershell
scripts\install-libvlc.ps1
```

Or:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install-libvlc.ps1
```

What it does:

- Downloads the latest VLC nightly runtime from VideoLAN.
- Downloads public libVLC headers from the VLC `master` branch.
- Installs headers into `libs/libvlc/include`.
- Installs `libvlc.lib` into `libs/libvlc/lib/vs`.
- Copies `libvlc.dll`, `libvlccore.dll`, `plugins`, and `lua` into each example `bin` folder.

The installer uses a temporary directory outside the addon tree while downloading and extracting archives, which helps avoid filesystem issues when the addon lives in a OneDrive-synced folder.

### Linux / WSL

Run:

```bash
bash scripts/install-libvlc.sh
```

This wrapper calls the same PowerShell installer, so it is mainly useful from WSL or Git Bash on Windows.

### Native Linux (Ubuntu)

Install VLC 4 and libVLC development files:

```bash
sudo add-apt-repository ppa:videolan/master-daily
sudo apt-get update
sudo apt-get install vlc libvlc-dev
```

The addon links against `libvlc` through `pkg-config` on Linux.

### Linux with Nvidia GPU

You may also need the Nvidia VA-API driver:

- [nvidia-vaapi-driver](https://github.com/elFarto/nvidia-vaapi-driver)

Example environment setup:

```bash
export LIBVA_DRIVER_NAME=nvidia
```

## Example dependency

The example app also depends on [ofxProjectM](https://github.com/Jonathhhan/ofxProjectM).
