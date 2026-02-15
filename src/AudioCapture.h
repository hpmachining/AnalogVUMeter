#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QList>
#include <QSettings>

#include <atomic>
#include <thread>

#include "VUBallistics.h"

#if defined(__APPLE__)
// Forward declarations for CoreAudio types
struct AudioQueueBuffer;
typedef struct OpaqueAudioQueue* AudioQueueRef;
#else
// Forward declarations for PulseAudio types
struct pa_mainloop;
struct pa_context;
struct pa_stream;
struct pa_sink_info;
struct pa_source_info;
#endif

class AudioCapture final : public QObject {
    Q_OBJECT

  public:
    // Device information structure for UI enumeration
    struct DeviceInfo {
        QString name;        // Human-readable name
        QString uid;         // Unique identifier (device UID on macOS, device name on Linux)
        int channels = 0;    // Number of channels
        bool isInput = true; // true for input devices, false for output
        bool isDefault = false;
    };

    struct Options final {
        int deviceIndex = -1; // unused in libpulse path, retained for compatibility
        double referenceDbfs = -14.0;
        double microphoneReferenceDbfs = 0.0; // Per-device reference for microphone
        double monitorReferenceDbfs = -14.0; // Per-device reference for monitor
        bool referenceDbfsOverride = false;
        int sampleRate = 48000;
        unsigned long framesPerBuffer = 512;

        // Optional: override device name (sink or source on Linux, device UID on macOS)
        QString deviceName;

        // 0 = sink monitor/system output, 1 = source/microphone
        int deviceType = 0;
    };

    explicit AudioCapture(const Options& options, QObject* parent = nullptr);
    ~AudioCapture() override;

    bool start(QString* errorOut = nullptr);
    void stop();

    // Switch to a different audio device at runtime
    bool switchDevice(const QString& deviceUID, QString* errorOut = nullptr);

    // Get the currently active device UID
    QString currentDeviceUID() const;

    // Get/set reference dBFS for 0 VU calibration
    double referenceDbfs() const;
    void setReferenceDbfs(double value);
    
    // Get/set per-device reference levels
    double microphoneReferenceDbfs() const;
    double monitorReferenceDbfs() const;
    void setMicrophoneReferenceDbfs(double value);
    void setMonitorReferenceDbfs(double value);
    
    // Load/save per-device reference levels from/to persistent storage
    void loadReferenceLevels();
    void saveReferenceLevels();
    
    // Get the effective reference dBFS based on device type
    double effectiveReferenceDbfs() const;

    float leftVuDb() const;
    float rightVuDb() const;

    // Get list of available input devices (for UI)
    static QList<DeviceInfo> enumerateInputDevices();

    // Legacy: string output for command line
    static QString listDevicesString();

  signals:
    void errorOccurred(const QString& message);
    void deviceChanged(const QString& deviceUID);

  private:
#if defined(__APPLE__)
    // CoreAudio callback
    static void audioInputCallback(void* inUserData,
                                   AudioQueueRef inAQ,
                                   AudioQueueBuffer* inBuffer,
                                   const void* inStartTime,
                                   unsigned int inNumberPacketDescriptions,
                                   const void* inPacketDescs);

    void processAudioBuffer(const float* data, unsigned int frames, unsigned int channels, float sampleRate);
#else
    // PulseAudio callbacks
    static void context_state_callback(pa_context* c, void* userdata);
    static void sink_info_callback(pa_context* c, const pa_sink_info* si, int is_last, void* userdata);
    static void source_info_callback(pa_context* c, const pa_source_info* si, int is_last, void* userdata);
    static void stream_state_callback(pa_stream* s, void* userdata);
    static void stream_read_callback(pa_stream* s, size_t length, void* userdata);
#endif

  private:
    Options options_;
    QString currentDeviceUID_;

    std::atomic<float> leftVuDb_{-22.0f};
    std::atomic<float> rightVuDb_{-22.0f};

    std::atomic<bool> running_{false};

#if defined(__APPLE__)
    AudioQueueRef audioQueue_ = nullptr;
    static constexpr int kNumBuffers = 3;
    AudioQueueBuffer* buffers_[kNumBuffers] = {};

    // Smoothed RMS values (persistent across callbacks)
    float rmsL_smooth_ = 0.0f;
    float rmsR_smooth_ = 0.0f;
    float prevL_ = 0.0f;
    float prevR_ = 0.0f;
    bool meterAwake_ = false;
#else
    std::thread thread_;
    pa_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
#endif

    VUBallistics ballisticsL_;
    VUBallistics ballisticsR_;
};
