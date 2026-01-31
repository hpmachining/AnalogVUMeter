#if defined(__APPLE__)

#include "AudioCapture.h"
#include "VuAudioDsp.h"

#include <algorithm>
#include <cmath>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

static constexpr float kMinVu = -22.0f;
static constexpr float kMaxVu = 3.0f;

AudioCapture::AudioCapture(const Options& options, QObject* parent)
    : QObject(parent), options_(options), currentDeviceUID_(options.deviceName), ballisticsL_(kMinVu),
      ballisticsR_(kMinVu) {}

AudioCapture::~AudioCapture() { stop(); }

bool AudioCapture::start(QString* errorOut) {
    if (running_.exchange(true)) {
        return true;
    }

    // Set up audio format - 32-bit float, stereo, at requested sample rate
    AudioStreamBasicDescription format = {};
    format.mSampleRate = options_.sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBitsPerChannel = 32;
    format.mChannelsPerFrame = 2; // Stereo
    format.mBytesPerFrame = format.mChannelsPerFrame * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = format.mBytesPerFrame;

    // Create the audio queue for input
    OSStatus status = AudioQueueNewInput(
        &format,
        [](void* inUserData,
           AudioQueueRef inAQ,
           AudioQueueBufferRef inBuffer,
           const AudioTimeStamp* inStartTime,
           UInt32 inNumberPacketDescriptions,
           const AudioStreamPacketDescription* inPacketDescs) {
            // Bridge to our static callback
            AudioCapture::audioInputCallback(inUserData,
                                             inAQ,
                                             reinterpret_cast<AudioQueueBuffer*>(inBuffer),
                                             inStartTime,
                                             inNumberPacketDescriptions,
                                             inPacketDescs);
        },
        this,
        nullptr, // Run loop (null = use internal thread)
        nullptr, // Run loop mode
        0,       // Reserved
        &audioQueue_);

    if (status != noErr) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create audio input queue: %1").arg(status);
        }
        running_.store(false, std::memory_order_relaxed);
        return false;
    }

    // If a specific device is requested or if we want system output (loopback)
    // Note: macOS system audio loopback requires special handling or third-party extensions
    if (!options_.deviceName.isEmpty()) {
        CFStringRef deviceUID = CFStringCreateWithCString(
            kCFAllocatorDefault, options_.deviceName.toUtf8().constData(), kCFStringEncodingUTF8);

        status = AudioQueueSetProperty(audioQueue_, kAudioQueueProperty_CurrentDevice, &deviceUID, sizeof(deviceUID));

        CFRelease(deviceUID);

        if (status != noErr) {
            if (errorOut) {
                *errorOut = QStringLiteral("Failed to set audio device: %1").arg(status);
            }
            AudioQueueDispose(audioQueue_, true);
            audioQueue_ = nullptr;
            running_.store(false, std::memory_order_relaxed);
            return false;
        }
        currentDeviceUID_ = options_.deviceName;
    } else {
        // Get the default input device UID
        AudioDeviceID defaultInput = 0;
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        UInt32 dataSize = sizeof(defaultInput);
        if (AudioObjectGetPropertyData(
                kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &defaultInput) == noErr) {
            CFStringRef deviceUID = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            dataSize = sizeof(deviceUID);
            if (AudioObjectGetPropertyData(defaultInput, &propertyAddress, 0, nullptr, &dataSize, &deviceUID) ==
                    noErr &&
                deviceUID) {
                currentDeviceUID_ = QString::fromCFString(deviceUID);
                CFRelease(deviceUID);
            }
        }
    }

    // Allocate and enqueue buffers
    UInt32 bufferSize = options_.framesPerBuffer * format.mBytesPerFrame;

    for (int i = 0; i < kNumBuffers; ++i) {
        status =
            AudioQueueAllocateBuffer(audioQueue_, bufferSize, reinterpret_cast<AudioQueueBufferRef*>(&buffers_[i]));
        if (status != noErr) {
            if (errorOut) {
                *errorOut = QStringLiteral("Failed to allocate audio buffer: %1").arg(status);
            }
            AudioQueueDispose(audioQueue_, true);
            audioQueue_ = nullptr;
            running_.store(false, std::memory_order_relaxed);
            return false;
        }

        status = AudioQueueEnqueueBuffer(audioQueue_, reinterpret_cast<AudioQueueBufferRef>(buffers_[i]), 0, nullptr);
        if (status != noErr) {
            if (errorOut) {
                *errorOut = QStringLiteral("Failed to enqueue audio buffer: %1").arg(status);
            }
            AudioQueueDispose(audioQueue_, true);
            audioQueue_ = nullptr;
            running_.store(false, std::memory_order_relaxed);
            return false;
        }
    }

    // Start the queue
    status = AudioQueueStart(audioQueue_, nullptr);
    if (status != noErr) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to start audio queue: %1").arg(status);
        }
        AudioQueueDispose(audioQueue_, true);
        audioQueue_ = nullptr;
        running_.store(false, std::memory_order_relaxed);
        return false;
    }

    if (errorOut) {
        *errorOut = QString();
    }
    return true;
}

void AudioCapture::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (audioQueue_) {
        AudioQueueStop(audioQueue_, true);
        AudioQueueDispose(audioQueue_, true);
        audioQueue_ = nullptr;
    }

    for (int i = 0; i < kNumBuffers; ++i) {
        buffers_[i] = nullptr;
    }
}

bool AudioCapture::switchDevice(const QString& deviceUID, QString* errorOut) {
    // Stop current capture
    stop();

    // Reset ballistics and smoothed values
    rmsL_smooth_ = 0.0f;
    rmsR_smooth_ = 0.0f;
    prevL_ = 0.0f;
    prevR_ = 0.0f;
    meterAwake_ = false;
    ballisticsL_.reset(kMinVu);
    ballisticsR_.reset(kMinVu);
    leftVuDb_.store(kMinVu, std::memory_order_relaxed);
    rightVuDb_.store(kMinVu, std::memory_order_relaxed);

    // Update options with new device
    options_.deviceName = deviceUID;

    // Restart with new device
    bool success = start(errorOut);

    if (success) {
        currentDeviceUID_ = deviceUID;
        emit deviceChanged(deviceUID);
    }

    return success;
}

QString AudioCapture::currentDeviceUID() const { return currentDeviceUID_; }

double AudioCapture::referenceDbfs() const { return options_.referenceDbfs; }

void AudioCapture::setReferenceDbfs(double value) {
    options_.referenceDbfs = value;
    options_.referenceDbfsOverride = true;
}

float AudioCapture::leftVuDb() const { return leftVuDb_.load(std::memory_order_relaxed); }

float AudioCapture::rightVuDb() const { return rightVuDb_.load(std::memory_order_relaxed); }

QList<AudioCapture::DeviceInfo> AudioCapture::enumerateInputDevices() {
    QList<DeviceInfo> result;

    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize);

    if (status != noErr) {
        return result;
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devices(deviceCount);

    status =
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, devices.data());

    if (status != noErr) {
        return result;
    }

    // Get default input device
    AudioDeviceID defaultInput = 0;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    dataSize = sizeof(defaultInput);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &defaultInput);

    for (AudioDeviceID deviceID : devices) {
        // Check if device has input channels
        propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
        propertyAddress.mScope = kAudioDevicePropertyScopeInput;

        dataSize = 0;
        status = AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nullptr, &dataSize);
        if (status != noErr)
            continue;

        std::vector<UInt8> bufferListData(dataSize);
        AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(bufferListData.data());

        status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
        if (status != noErr)
            continue;

        UInt32 inputChannels = 0;
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i) {
            inputChannels += bufferList->mBuffers[i].mNumberChannels;
        }

        if (inputChannels > 0) {
            DeviceInfo info;
            info.channels = static_cast<int>(inputChannels);
            info.isInput = true;
            info.isDefault = (deviceID == defaultInput);

            // Get device name
            CFStringRef deviceName = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
            propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
            dataSize = sizeof(deviceName);
            if (AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceName) == noErr &&
                deviceName) {
                info.name = QString::fromCFString(deviceName);
                CFRelease(deviceName);
            } else {
                info.name = "Unknown Device";
            }

            // Get device UID
            CFStringRef deviceUID = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            dataSize = sizeof(deviceUID);
            if (AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceUID) == noErr &&
                deviceUID) {
                info.uid = QString::fromCFString(deviceUID);
                CFRelease(deviceUID);
            }

            result.append(info);
        }
    }

    return result;
}

QString AudioCapture::listDevicesString() {
    QString out;
    out += "CoreAudio devices:\n\n";

    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize);

    if (status != noErr) {
        return "Failed to get audio devices\n";
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devices(deviceCount);

    status =
        AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, devices.data());

    if (status != noErr) {
        return "Failed to enumerate audio devices\n";
    }

    // Get default input device
    AudioDeviceID defaultInput = 0;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    dataSize = sizeof(defaultInput);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &defaultInput);

    // Get default output device
    AudioDeviceID defaultOutput = 0;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    dataSize = sizeof(defaultOutput);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &defaultOutput);

    out += "=== Input Devices ===\n";

    for (AudioDeviceID deviceID : devices) {
        // Check if device has input channels
        propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
        propertyAddress.mScope = kAudioDevicePropertyScopeInput;

        dataSize = 0;
        status = AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nullptr, &dataSize);
        if (status != noErr)
            continue;

        std::vector<UInt8> bufferListData(dataSize);
        AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(bufferListData.data());

        status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
        if (status != noErr)
            continue;

        UInt32 inputChannels = 0;
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i) {
            inputChannels += bufferList->mBuffers[i].mNumberChannels;
        }

        if (inputChannels > 0) {
            // Get device name
            CFStringRef deviceName = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
            propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
            dataSize = sizeof(deviceName);
            AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceName);

            // Get device UID
            CFStringRef deviceUID = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            dataSize = sizeof(deviceUID);
            AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceUID);

            QString name = deviceName ? QString::fromCFString(deviceName) : "Unknown";
            QString uid = deviceUID ? QString::fromCFString(deviceUID) : "";
            bool isDefault = (deviceID == defaultInput);

            out += QString("Input: %1%2\n").arg(name).arg(isDefault ? "   [DEFAULT]" : "");
            out += QString("  UID: %1\n").arg(uid);
            out += QString("  Channels: %1\n\n").arg(inputChannels);

            if (deviceName)
                CFRelease(deviceName);
            if (deviceUID)
                CFRelease(deviceUID);
        }
    }

    out += "=== Output Devices ===\n";

    for (AudioDeviceID deviceID : devices) {
        // Check if device has output channels
        propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
        propertyAddress.mScope = kAudioDevicePropertyScopeOutput;

        dataSize = 0;
        status = AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nullptr, &dataSize);
        if (status != noErr)
            continue;

        std::vector<UInt8> bufferListData(dataSize);
        AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(bufferListData.data());

        status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
        if (status != noErr)
            continue;

        UInt32 outputChannels = 0;
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i) {
            outputChannels += bufferList->mBuffers[i].mNumberChannels;
        }

        if (outputChannels > 0) {
            // Get device name
            CFStringRef deviceName = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
            propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
            dataSize = sizeof(deviceName);
            AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceName);

            // Get device UID
            CFStringRef deviceUID = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            dataSize = sizeof(deviceUID);
            AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceUID);

            QString name = deviceName ? QString::fromCFString(deviceName) : "Unknown";
            QString uid = deviceUID ? QString::fromCFString(deviceUID) : "";
            bool isDefault = (deviceID == defaultOutput);

            out += QString("Output: %1%2\n").arg(name).arg(isDefault ? "   [DEFAULT]" : "");
            out += QString("  UID: %1\n").arg(uid);
            out += QString("  Channels: %1\n\n").arg(outputChannels);

            if (deviceName)
                CFRelease(deviceName);
            if (deviceUID)
                CFRelease(deviceUID);
        }
    }

    out += "\nUsage:\n";
    out += "  --device-type 0   Use system output (requires loopback driver like BlackHole)\n";
    out += "  --device-type 1   Use microphone input\n";
    out += "  --device-name <uid>   Use specific device by UID\n";
    out += "\nNote: To capture system audio on macOS, install a loopback driver like\n";
    out += "BlackHole (https://github.com/ExistentialAudio/BlackHole) and configure\n";
    out += "it as a multi-output device in Audio MIDI Setup.\n";

    return out;
}

// -------- CoreAudio callback --------

void AudioCapture::audioInputCallback(void* inUserData,
                                      AudioQueueRef inAQ,
                                      AudioQueueBuffer* inBuffer,
                                      const void* inStartTime,
                                      unsigned int inNumberPacketDescriptions,
                                      const void* inPacketDescs) {
    (void)inStartTime;
    (void)inNumberPacketDescriptions;
    (void)inPacketDescs;

    auto* self = static_cast<AudioCapture*>(inUserData);

    if (!self->running_.load(std::memory_order_relaxed)) {
        return;
    }

    AudioQueueBufferRef buffer = reinterpret_cast<AudioQueueBufferRef>(inBuffer);

    const float* data = static_cast<const float*>(buffer->mAudioData);
    const unsigned int frames = buffer->mAudioDataByteSize / (2 * sizeof(float)); // stereo

    self->processAudioBuffer(data, frames, 2, static_cast<float>(self->options_.sampleRate));

    // Re-enqueue the buffer
    AudioQueueEnqueueBuffer(inAQ, buffer, 0, nullptr);
}

void AudioCapture::processAudioBuffer(const float* data, unsigned int frames, unsigned int channels, float sampleRate) {
    VuReferenceOptions ref;
    ref.referenceDbfs = options_.referenceDbfs;
    ref.referenceDbfsOverride = options_.referenceDbfsOverride;
    ref.deviceType = options_.deviceType;

    VuAudioDspState dspState;
    dspState.prevL = prevL_;
    dspState.prevR = prevR_;
    dspState.rmsL_smooth = rmsL_smooth_;
    dspState.rmsR_smooth = rmsR_smooth_;
    dspState.meterAwake = meterAwake_;

    float vuL = kMinVu;
    float vuR = kMinVu;
    processInterleavedFloatAudioToVuDb(data,
                                        frames,
                                        channels,
                                        sampleRate,
                                        ref,
                                        ballisticsL_,
                                        ballisticsR_,
                                        dspState,
                                        kMinVu,
                                        kMaxVu,
                                        vuL,
                                        vuR);

    prevL_ = dspState.prevL;
    prevR_ = dspState.prevR;
    rmsL_smooth_ = dspState.rmsL_smooth;
    rmsR_smooth_ = dspState.rmsR_smooth;
    meterAwake_ = dspState.meterAwake;

    leftVuDb_.store(vuL, std::memory_order_relaxed);
    rightVuDb_.store(vuR, std::memory_order_relaxed);
}

#endif // __APPLE__
