#if defined(__APPLE__)

#include "AudioCapture.h"

#include <algorithm>
#include <cmath>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

static constexpr float kMinVu = -22.0f;
static constexpr float kMaxVu = 3.0f;

AudioCapture::AudioCapture(const Options& options, QObject* parent)
    : QObject(parent), options_(options), currentDeviceUID_(options.deviceName), 
      ballisticsL_(kMinVu), ballisticsR_(kMinVu) {}

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
    format.mChannelsPerFrame = 2;  // Stereo
    format.mBytesPerFrame = format.mChannelsPerFrame * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = format.mBytesPerFrame;

    // Create the audio queue for input
    OSStatus status = AudioQueueNewInput(
        &format,
        [](void* inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer,
           const AudioTimeStamp* inStartTime, UInt32 inNumberPacketDescriptions,
           const AudioStreamPacketDescription* inPacketDescs) {
            // Bridge to our static callback
            AudioCapture::audioInputCallback(inUserData, inAQ, 
                reinterpret_cast<AudioQueueBuffer*>(inBuffer),
                inStartTime, inNumberPacketDescriptions, inPacketDescs);
        },
        this,
        nullptr,  // Run loop (null = use internal thread)
        nullptr,  // Run loop mode
        0,        // Reserved
        &audioQueue_
    );

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
            kCFAllocatorDefault,
            options_.deviceName.toUtf8().constData(),
            kCFStringEncodingUTF8
        );
        
        status = AudioQueueSetProperty(
            audioQueue_,
            kAudioQueueProperty_CurrentDevice,
            &deviceUID,
            sizeof(deviceUID)
        );
        
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
            kAudioHardwarePropertyDefaultInputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 dataSize = sizeof(defaultInput);
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &defaultInput) == noErr) {
            CFStringRef deviceUID = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            dataSize = sizeof(deviceUID);
            if (AudioObjectGetPropertyData(defaultInput, &propertyAddress, 0, nullptr, &dataSize, &deviceUID) == noErr && deviceUID) {
                currentDeviceUID_ = QString::fromCFString(deviceUID);
                CFRelease(deviceUID);
            }
        }
    }

    // Allocate and enqueue buffers
    UInt32 bufferSize = options_.framesPerBuffer * format.mBytesPerFrame;
    
    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(audioQueue_, bufferSize, 
            reinterpret_cast<AudioQueueBufferRef*>(&buffers_[i]));
        if (status != noErr) {
            if (errorOut) {
                *errorOut = QStringLiteral("Failed to allocate audio buffer: %1").arg(status);
            }
            AudioQueueDispose(audioQueue_, true);
            audioQueue_ = nullptr;
            running_.store(false, std::memory_order_relaxed);
            return false;
        }
        
        status = AudioQueueEnqueueBuffer(audioQueue_, 
            reinterpret_cast<AudioQueueBufferRef>(buffers_[i]), 0, nullptr);
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

QString AudioCapture::currentDeviceUID() const {
    return currentDeviceUID_;
}

double AudioCapture::referenceDbfs() const {
    return options_.referenceDbfs;
}

void AudioCapture::setReferenceDbfs(double value) {
    options_.referenceDbfs = value;
    options_.referenceDbfsOverride = true;
}

float AudioCapture::leftVuDb() const { return leftVuDb_.load(std::memory_order_relaxed); }

float AudioCapture::rightVuDb() const { return rightVuDb_.load(std::memory_order_relaxed); }

QList<AudioCapture::DeviceInfo> AudioCapture::enumerateInputDevices() {
    QList<DeviceInfo> result;

    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &dataSize
    );

    if (status != noErr) {
        return result;
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devices(deviceCount);

    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &dataSize,
        devices.data()
    );

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
        if (status != noErr) continue;

        std::vector<UInt8> bufferListData(dataSize);
        AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(bufferListData.data());
        
        status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
        if (status != noErr) continue;

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
            if (AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceName) == noErr && deviceName) {
                info.name = QString::fromCFString(deviceName);
                CFRelease(deviceName);
            } else {
                info.name = "Unknown Device";
            }

            // Get device UID
            CFStringRef deviceUID = nullptr;
            propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
            dataSize = sizeof(deviceUID);
            if (AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &deviceUID) == noErr && deviceUID) {
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
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &dataSize
    );

    if (status != noErr) {
        return "Failed to get audio devices\n";
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devices(deviceCount);

    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &dataSize,
        devices.data()
    );

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
        if (status != noErr) continue;

        std::vector<UInt8> bufferListData(dataSize);
        AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(bufferListData.data());
        
        status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
        if (status != noErr) continue;

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

            if (deviceName) CFRelease(deviceName);
            if (deviceUID) CFRelease(deviceUID);
        }
    }

    out += "=== Output Devices ===\n";
    
    for (AudioDeviceID deviceID : devices) {
        // Check if device has output channels
        propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
        propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
        
        dataSize = 0;
        status = AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nullptr, &dataSize);
        if (status != noErr) continue;

        std::vector<UInt8> bufferListData(dataSize);
        AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(bufferListData.data());
        
        status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, bufferList);
        if (status != noErr) continue;

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

            if (deviceName) CFRelease(deviceName);
            if (deviceUID) CFRelease(deviceUID);
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
    const unsigned int frames = buffer->mAudioDataByteSize / (2 * sizeof(float));  // stereo
    
    self->processAudioBuffer(data, frames, 2, static_cast<float>(self->options_.sampleRate));

    // Re-enqueue the buffer
    AudioQueueEnqueueBuffer(inAQ, buffer, 0, nullptr);
}

void AudioCapture::processAudioBuffer(const float* data, unsigned int frames, unsigned int channels, float sampleRate) {
    if (frames == 0) {
        return;
    }

    // --- Compute raw RMS for this buffer ---
    double sumL = 0.0;
    double sumR = 0.0;

    for (unsigned int i = 0; i < frames; ++i) {
        const float rawL = data[i * channels + 0];
        const float rawR = (channels > 1) ? data[i * channels + 1] : rawL;

        // Transient pre-emphasis (very subtle)
        const float l = rawL + 0.15f * (rawL - prevL_);
        const float r = rawR + 0.15f * (rawR - prevR_);

        prevL_ = rawL;
        prevR_ = rawR;

        sumL += static_cast<double>(l) * static_cast<double>(l);
        sumR += static_cast<double>(r) * static_cast<double>(r);
    }

    float rmsL = std::sqrt(static_cast<float>(sumL / frames));
    float rmsR = std::sqrt(static_cast<float>(sumR / frames));

    // --- Vintage VU RMS integration (250 ms) ---
    const float wakeThreshold = 0.002f; // about -54 dBFS

    if (rmsL > wakeThreshold) {
        rmsL_smooth_ = rmsL * rmsL;
    }
    if (rmsR > wakeThreshold) {
        rmsR_smooth_ = rmsR * rmsR;
    }

    float vuTau = 0.020f;
    float dt = static_cast<float>(frames) / sampleRate;
    dt = std::min(dt, 0.050f); // clamp to 50 ms
    const float alpha = std::exp(-dt / vuTau);

    rmsL_smooth_ = alpha * rmsL_smooth_ + (1.0f - alpha) * (rmsL * rmsL);
    rmsR_smooth_ = alpha * rmsR_smooth_ + (1.0f - alpha) * (rmsR * rmsR);

    float rmsL_vu = std::sqrt(rmsL_smooth_);
    float rmsR_vu = std::sqrt(rmsR_smooth_);

    // --- Noise floor applied to smoothed RMS ---
    const float noiseFloor = 0.001f;
    if (rmsL_vu < noiseFloor)
        rmsL_vu = 0.0f;
    if (rmsR_vu < noiseFloor)
        rmsR_vu = 0.0f;

    // --- Convert to dBFS ---
    const float eps = 1e-12f;
    const float dbfsL = 20.0f * std::log10(std::max(rmsL_vu, eps));
    const float dbfsR = 20.0f * std::log10(std::max(rmsR_vu, eps));

    // --- Reference level for hi-fi VU behavior ---
    float effectiveRefDbfs;
    if (options_.referenceDbfsOverride) {
        effectiveRefDbfs = static_cast<float>(options_.referenceDbfs);
    } else if (options_.deviceType == 1) {
        // Microphone mode
        effectiveRefDbfs = -0.0f;
    } else {
        // System output mode
        effectiveRefDbfs = -14.0f;
    }

    float targetVuL = dbfsL - effectiveRefDbfs;
    float targetVuR = dbfsR - effectiveRefDbfs;

    if (!meterAwake_ && (rmsL_vu > 0.002f || rmsR_vu > 0.002f)) {
        ballisticsL_.reset(targetVuL);
        ballisticsR_.reset(targetVuR);
        meterAwake_ = true;
    }

    // --- Apply ballistics using per-callback dt ---
    float vuL = ballisticsL_.process(targetVuL, dt);
    float vuR = ballisticsR_.process(targetVuR, dt);

    // --- Clamp to meter scale ---
    vuL = std::clamp(vuL, kMinVu, kMaxVu);
    vuR = std::clamp(vuR, kMinVu, kMaxVu);

    leftVuDb_.store(vuL, std::memory_order_relaxed);
    rightVuDb_.store(vuR, std::memory_order_relaxed);
}

#endif // __APPLE__
