# AnalogVUMeter

A cross-platform desktop application that visually replicates a classic analog stereo VU meter (needle-style) using Qt 6 custom painting and native audio APIs.

## Features

- Stereo (Left/Right) analog VU meters with a retro hardware aesthetic
- RMS-based level measurement with classic VU ballistics
  - Vintage hi-fi style attack/decay
  - Slight transient overshoot
  - Subtle needle "life" (very small jitter)
- ~60 Hz refresh rate
- Multi-threaded audio capture (non-blocking GUI)
- System output monitoring (captures audio playing through speakers)
- Microphone input support
- Runtime-importable custom meter skins
- Cross-platform support: Linux (PulseAudio/PipeWire) and macOS (CoreAudio)

## Requirements

### All Platforms

- C++20 compatible compiler (GCC 11+, Clang 14+, or Apple Clang)
- CMake 3.20 or later
- Qt 6 (Widgets module)
- libzip

### Platform-Specific

**Linux:**
- PulseAudio development libraries (`libpulse`)

**macOS:**
- Xcode Command Line Tools
- CoreAudio and AudioToolbox frameworks (included with macOS)

## Building from Source

### Arch Linux (Automated Build)

The simplest method on Arch Linux is to use the provided PKGBUILD, which handles dependencies, building, and installation automatically:

```bash
# Install build tools
sudo pacman -S base-devel
```
```bash
# Create a clean build directory
mkdir analogvumeter-build && cd analogvumeter-build

# Download PKGBUILD from repository
curl -O https://raw.githubusercontent.com/hpmachining/AnalogVUMeter/main/PKGBUILD

# Build and create package (installs dependencies automatically)
makepkg -s
```

```bash
# Install the package
sudo pacman -U analogvumeter-*.pkg.tar.zst
```

### Installing Dependencies

If building manually, install the required dependencies for your platform:

#### Debian/Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config \
                        qt6-base-dev libpulse-dev libzip-dev
```

#### Fedora

```bash
sudo dnf install -y @development-tools cmake pkgconf-pkg-config \
                    qt6-qtbase-devel pulseaudio-libs-devel libzip-devel
```

#### Arch Linux

```bash
sudo pacman -S base-devel cmake qt6-base libpulse libzip
```

#### macOS

```bash
# Install Xcode Command Line Tools (if not already installed)
xcode-select --install

# Install dependencies via Homebrew
brew install cmake qt@6 libzip
```

### Compiling

Once dependencies are installed, build the application:

```bash
# Configure the build
cmake -S . -B build -DCMAKE_BUILD_TYPE=None

# On macOS, you may need to specify Qt location:
# cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"

# Build
cmake --build build
```

## Running the Application

### Linux

```bash
./build/analog_vu_meter
```

### macOS

```bash
# Run the app bundle
open ./build/analog_vu_meter.app

# Or run the binary directly
./build/analog_vu_meter.app/Contents/MacOS/analog_vu_meter
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--list-devices` | Display available audio devices and exit |
| `--device-type <0\|1>` | Select device type: `0` = system output, `1` = microphone |
| `--device-name <n>` | Specify device by name (PulseAudio on Linux) or UID (CoreAudio on macOS) |
| `--ref-dbfs <db>` | Set reference level in dBFS for 0 VU mark |

## Usage

### Audio Source Selection

**System Output Monitoring (Default on Linux):**
```bash
./build/analog_vu_meter --device-type 0
```

**Microphone Input (Default on macOS):**
```bash
./build/analog_vu_meter --device-type 1
```

**Specific Device:**
```bash
# Linux (PulseAudio device name)
./build/analog_vu_meter --device-name "alsa_output.pci-0000_00_1b.0.analog-stereo.monitor"

# macOS (CoreAudio device UID)
./build/analog_vu_meter --device-name "BuiltInMicrophoneDevice"
```

**List Available Devices:**
```bash
# List all devices (useful for finding exact device names/UIDs)
./build/analog_vu_meter --list-devices
```

### Calibration

The meter displays audio levels using a classic VU meter scale:

- **Scale Range:** Clamped to the minimum and maximum values of the selected meter skin
- **Default Reference Levels:**
  - System output: `-14 dBFS` = 0 VU
  - Microphone input: `0 dBFS` = 0 VU
- **Scale Spacing:** Manually shaped to resemble authentic analog meter faces

## Platform-Specific Notes

### Linux

- Uses the PulseAudio client library (`libpulse`) for audio capture
- System audio monitoring is available when a PulseAudio‑compatible server is running (PipeWire‑Pulse or PulseAudio)
- No additional configuration is required on systems using PipeWire‑Pulse or PulseAudio

### macOS

- Uses CoreAudio's AudioQueue API for audio capture
- Microphone access requires system permission (prompted on first run)
- Builds as a native `.app` bundle for macOS integration

#### System Audio Capture on macOS

Unlike Linux, macOS does not provide built-in system audio loopback. To monitor system audio output:

1. **Install BlackHole** (free, open-source loopback driver):
   - Download from: https://github.com/ExistentialAudio/BlackHole
   - Install the 2ch or 16ch version

2. **Create a Multi-Output Device:**
   - Open **Audio MIDI Setup** (`/Applications/Utilities/`)
   - Click the **+** button → **Create Multi-Output Device**
   - Check both your primary audio output (speakers/headphones) AND BlackHole
   - Set this multi-output device as your system output in **System Settings → Sound**

3. **Configure the VU Meter:**
   ```bash
   # List devices to find BlackHole UID
   ./build/analog_vu_meter --list-devices
   
   # Run with BlackHole as input
   ./build/analog_vu_meter --device-name "BlackHole2ch_UID"
   ```

## Contributing

Contributions are welcome! Whether you're fixing bugs, improving calibration, adding features, or polishing documentation, feel free to open a pull request. Small improvements are just as valuable as major features.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.