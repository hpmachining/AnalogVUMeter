#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

#include "VUBallistics.h"

// Forward declarations for PulseAudio types
struct pa_mainloop;
struct pa_context;
struct pa_stream;
struct pa_sink_info;
struct pa_source_info;

class AudioCapture final : public QObject {
    Q_OBJECT

  public:
    struct Options final {
        int deviceIndex = -1; // unused in libpulse path, retained for compatibility
        double referenceDbfs = -18.0;
        bool referenceDbfsOverride = false;
        int sampleRate = 48000;
        unsigned long framesPerBuffer = 512;

        // Optional: override device name (sink or source)
        QString deviceName;

        // 0 = sink monitor (system output), 1 = source (microphone)
        int deviceType = 0;
    };

    explicit AudioCapture(const Options& options, QObject* parent = nullptr);
    ~AudioCapture() override;

    bool start(QString* errorOut = nullptr);
    void stop();

    float leftVuDb() const;
    float rightVuDb() const;

    static QString listDevicesString();

  signals:
    void errorOccurred(const QString& message);

  private:
    // PulseAudio callbacks
    static void context_state_callback(pa_context* c, void* userdata);
    static void sink_info_callback(pa_context* c, const pa_sink_info* si, int is_last, void* userdata);
    static void source_info_callback(pa_context* c, const pa_source_info* si, int is_last, void* userdata);
    static void stream_state_callback(pa_stream* s, void* userdata);
    static void stream_read_callback(pa_stream* s, size_t length, void* userdata);

  private:
    Options options_;

    std::atomic<float> leftVuDb_{-22.0f};
    std::atomic<float> rightVuDb_{-22.0f};

    std::atomic<bool> running_{false};
    std::thread thread_;

    // Replaces PortAudio stream pointer
    pa_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;

    VUBallistics ballisticsL_;
    VUBallistics ballisticsR_;
};
