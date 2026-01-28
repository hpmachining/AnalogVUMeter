# AnalogVUMeterQt

A cross-platform desktop application that visually replicates a classic analog stereo VU meter (needle-style) using Qt 6 custom painting and native audio APIs.

## Features

- Stereo (Left/Right) analog VU meters with a retro hardware aesthetic
- RMS-based level measurement
- Classic VU ballistics
  - vintage hi-fi style attack/decay
  - slight transient overshoot
  - subtle needle "life" (very small jitter)
- Refresh rate ~60 Hz
- Audio capture runs outside the GUI thread
- **System output monitoring** (captures what you hear through speakers)
- Microphone input support
- Cross-platform: Linux (PulseAudio/PipeWire) and macOS (CoreAudio)

## Dependencies

### Common Requirements

- C++20 compiler (GCC 11+, Clang 14+, or Apple Clang)
- CMake 3.20+
- Qt 6 (Widgets)

### Linux

- PulseAudio (libpulse)

### macOS

- CoreAudio framework (included with macOS)
- AudioToolbox framework (included with macOS)

## Installation

### macOS

```bash
# Install Xcode Command Line Tools (if not already installed)
xcode-select --install

# Install dependencies via Homebrew
brew install cmake qt@6

# You may need to add Qt to your PATH or set CMAKE_PREFIX_PATH
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
```

### Linux (Debian/Ubuntu)

```bash
sudo apt-get install -y build-essential cmake pkg-config qt6-base-dev libpulse-dev
```

### Linux (Fedora)

```bash
sudo dnf install -y @development-tools cmake pkgconf-pkg-config qt6-qtbase-devel pulseaudio-libs-devel
```

### Linux (Arch Linux)

```bash
sudo pacman -S base-devel cmake pkgconf qt6-base pulseaudio
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

### Linux

```bash
./build/analog_vu_meter
```

### macOS

```bash
# Run the app bundle
open ./build/analog_vu_meter.app

# Or run directly
./build/analog_vu_meter.app/Contents/MacOS/analog_vu_meter
```

### Audio Source Options

**Microphone Input (Default on macOS)**:
```bash
./build/analog_vu_meter --device-type 1
```

**System Output (Linux)**:
```bash
./build/analog_vu_meter --device-type 0
```

**System Output (macOS)** - Requires a loopback driver like BlackHole:
```bash
# First install BlackHole: https://github.com/ExistentialAudio/BlackHole
# Then configure it as a multi-output device in Audio MIDI Setup
./build/analog_vu_meter --device-name "BlackHoleUID"
```

**Specific Device**:
```bash
# Linux (PulseAudio device name)
./build/analog_vu_meter --device-name "alsa_output.pci-0000_00_1b.0.analog-stereo.monitor"

# macOS (CoreAudio device UID)
./build/analog_vu_meter --device-name "BuiltInMicrophoneDevice"
```

**List available devices**:
```bash
./build/analog_vu_meter --list-devices
```

## macOS System Audio Capture

Unlike Linux, macOS does not provide built-in system audio loopback. To capture system audio on macOS:

1. **Install BlackHole** (free, open-source):
   - Download from: https://github.com/ExistentialAudio/BlackHole
   - Install the 2ch or 16ch version

2. **Create a Multi-Output Device**:
   - Open **Audio MIDI Setup** (in /Applications/Utilities/)
   - Click the **+** button and select "Create Multi-Output Device"
   - Check both your speakers/headphones AND BlackHole
   - Set this as your system output in System Settings > Sound

3. **Run the VU meter with BlackHole**:
   ```bash
   ./build/analog_vu_meter --list-devices  # Find the BlackHole UID
   ./build/analog_vu_meter --device-name "BlackHole2ch_UID"
   ```

## Calibration / assumptions

- Meter scale is clamped to `[-22, +3]` VU for display
- Default reference is mode-dependent:
  - system output (sink monitor): `-14 dBFS` for `0 VU`
  - microphone input: `0 dBFS` for `0 VU`
- The scale spacing is manually shaped to resemble a real analog meter face

You can change the reference with:

```bash
./build/analog_vu_meter --ref-dbfs -18
```

## Command Line Options

- `--list-devices` - Show available audio devices and usage
- `--device-type <0|1>` - 0=system output, 1=microphone
- `--device-name <name>` - Specific device (PulseAudio name on Linux, CoreAudio UID on macOS)
- `--ref-dbfs <db>` - Override reference dBFS for 0 VU

## Platform Notes

### macOS
- Uses CoreAudio's AudioQueue API for audio capture
- Requires microphone permission (will be prompted on first run)
- System audio capture requires a third-party loopback driver (e.g., BlackHole)
- Creates a proper .app bundle

### Linux
- Uses PulseAudio for audio capture
- Full PipeWire compatibility
- System output monitoring works out of the box via sink monitors

## Contributions

If you have improvements (bug fixes, calibration tweaks, new ideas), feel free to open a pull request. Even small cleanups are welcome.

## License

MIT. See `LICENSE`.
