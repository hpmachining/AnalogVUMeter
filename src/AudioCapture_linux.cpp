#if defined(__linux__)

#include "AudioCapture.h"
#include "VuAudioDsp.h"

#include <chrono>
#include <thread>

#include <QByteArray>
#include <QDebug>
#include <QPair>
#include <pulse/pulseaudio.h>

// Named constants instead of magic numbers
static constexpr float kAudioFloorVu = -96.0f;
static constexpr float kAudioCeilingVu = 6.0f;
static constexpr int kFragmentSizeMs = 10;
static constexpr int kContextTimeoutMs = 10000;
static constexpr int kContextPollIntervalMs = 100;

// Device type constants
// These distinguish between capturing system playback vs external input
static constexpr int kDeviceTypeMonitor = 0;    // Monitor source (captures system audio output)
static constexpr int kDeviceTypeMicrophone = 1; // Regular source (microphone, line-in, etc.)

// -------- Constructor / Destructor --------

AudioCapture::AudioCapture(const Options& options, QObject* parent)
    : QObject(parent), options_(options), currentDeviceUID_(options.deviceName), deviceType_(options.deviceType),
      ballisticsL_(kAudioFloorVu), ballisticsR_(kAudioFloorVu), dspState_(new VuAudioDspState{}) {
    loadReferenceLevels();
}

AudioCapture::~AudioCapture() {
    stop();
    delete dspState_;
}

// -------- Start / Stop --------

bool AudioCapture::start(QString* errorOut) {
    if (running_.exchange(true)) {
        return true;
    }

    // Initialize PulseAudio mainloop and context
    mainloop_ = pa_mainloop_new();
    if (!mainloop_) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create PulseAudio mainloop");
        }
        running_.store(false, std::memory_order_relaxed);
        return false;
    }

    context_ = pa_context_new(pa_mainloop_get_api(mainloop_), "Analog VU Meter");
    if (!context_) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create PulseAudio context");
        }
        pa_mainloop_free(mainloop_);
        mainloop_ = nullptr;
        running_.store(false, std::memory_order_relaxed);
        return false;
    }

    pa_context_set_state_callback(context_, &AudioCapture::context_state_callback, this);

    int connect_result = pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
    if (connect_result < 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to connect to PulseAudio: %1").arg(pa_strerror(connect_result));
        }
        pa_context_unref(context_);
        context_ = nullptr;
        pa_mainloop_free(mainloop_);
        mainloop_ = nullptr;
        running_.store(false, std::memory_order_relaxed);
        return false;
    }

    // Start the PulseAudio mainloop in a separate thread
    thread_ = std::thread([this]() {
        int ret = 0;
        pa_mainloop_run(mainloop_, &ret);
    });

    // Wait for context to be ready with timeout
    bool contextReady = false;
    int maxWait = kContextTimeoutMs / kContextPollIntervalMs;
    int waitCount = 0;

    while (!contextReady && waitCount < maxWait) {
        pa_context_state_t state = pa_context_get_state(context_);
        if (state == PA_CONTEXT_READY) {
            contextReady = true;
        } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            if (errorOut) {
                *errorOut = QStringLiteral("PulseAudio context failed to initialize");
            }
            // Clean up properly
            pa_mainloop_quit(mainloop_, 0);
            if (thread_.joinable()) {
                thread_.join();
            }
            pa_context_unref(context_);
            context_ = nullptr;
            pa_mainloop_free(mainloop_);
            mainloop_ = nullptr;
            running_.store(false, std::memory_order_relaxed);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kContextPollIntervalMs));
        waitCount++;
    }

    if (!contextReady) {
        if (errorOut) {
            *errorOut = QStringLiteral("PulseAudio context initialization timed out");
        }
        // Clean up properly
        pa_mainloop_quit(mainloop_, 0);
        if (thread_.joinable()) {
            thread_.join();
        }
        pa_context_unref(context_);
        context_ = nullptr;
        pa_mainloop_free(mainloop_);
        mainloop_ = nullptr;
        running_.store(false, std::memory_order_relaxed);
        return false;
    }

    // Look up the device and create stream
    if (!options_.deviceName.isEmpty()) {
        // Check if it's a monitor source (ends with .monitor)
        if (options_.deviceName.endsWith(".monitor")) {
            // Extract the sink name from monitor source name
            QString sinkName = options_.deviceName;
            sinkName.chop(8); // Remove ".monitor"

            // Set device type to system output (monitor)
            deviceType_.store(kDeviceTypeMonitor, std::memory_order_relaxed);

            // Get sink info to access monitor source
            pa_operation* op = pa_context_get_sink_info_by_name(
                context_, sinkName.toUtf8().constData(), &AudioCapture::sink_info_callback, this);
            if (op) {
                pa_operation_unref(op);
            }
        } else {
            // Regular source (microphone)
            // Set device type to microphone (external input)
            deviceType_.store(kDeviceTypeMicrophone, std::memory_order_relaxed);

            pa_operation* op = pa_context_get_source_info_by_name(
                context_, options_.deviceName.toUtf8().constData(), &AudioCapture::source_info_callback, this);
            if (op) {
                pa_operation_unref(op);
            }
        }
    } else {
        // No device specified, default to monitor of default sink (audio output)
        // Set device type to system output (monitor)
        deviceType_.store(kDeviceTypeMonitor, std::memory_order_relaxed);

        pa_operation* op = pa_context_get_server_info(
            context_,
            [](pa_context* /*ctx*/, const pa_server_info* info, void* userdata) {
                auto* self = static_cast<AudioCapture*>(userdata);
                if (info && info->default_sink_name) {
                    // Get sink info to access its monitor source
                    pa_operation* op2 = pa_context_get_sink_info_by_name(
                        self->context_, info->default_sink_name, &AudioCapture::sink_info_callback, self);
                    if (op2) {
                        pa_operation_unref(op2);
                    }
                }
            },
            this);
        if (op) {
            pa_operation_unref(op);
        }
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

    if (mainloop_) {
        pa_mainloop_quit(mainloop_, 0);
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    if (stream_) {
        pa_stream_disconnect(stream_);
        pa_stream_unref(stream_);
        stream_ = nullptr;
    }
    if (context_) {
        // Only disconnect if context is still in a valid state
        pa_context_state_t state = pa_context_get_state(context_);
        if (state == PA_CONTEXT_READY || state == PA_CONTEXT_CONNECTING || state == PA_CONTEXT_AUTHORIZING) {
            pa_context_disconnect(context_);
        }
        pa_context_unref(context_);
        context_ = nullptr;
    }
    if (mainloop_) {
        pa_mainloop_free(mainloop_);
        mainloop_ = nullptr;
    }
}

// -------- Device Switching --------

bool AudioCapture::switchDevice(const QString& deviceUID, QString* errorOut) {
    // Stop current capture
    stop();

    // Reset ballistics and VU meters
    ballisticsL_.reset(kAudioFloorVu);
    ballisticsR_.reset(kAudioFloorVu);
    leftVuDb_.store(kAudioFloorVu, std::memory_order_relaxed);
    rightVuDb_.store(kAudioFloorVu, std::memory_order_relaxed);

    // Update options with new device
    options_.deviceName = deviceUID;
    currentDeviceUID_ = deviceUID.isEmpty() ? QString() : deviceUID;

    // Restart with new device
    bool success = start(errorOut);

    if (success) {
        // If using default device, update currentDeviceUID_ with actual device
        if (deviceUID.isEmpty()) {
            // Using default - the currentDeviceUID_ will be set by callbacks
            // For now, indicate it's the default monitor
            currentDeviceUID_ = QStringLiteral("[default monitor]");
        }
        emit deviceChanged(currentDeviceUID_);
    }

    return success;
}

// -------- Getters / Setters --------

QString AudioCapture::currentDeviceUID() const { return currentDeviceUID_; }

double AudioCapture::referenceDbfs() const { return options_.referenceDbfs; }

void AudioCapture::setReferenceDbfs(double value) {
    // Store per-device reference level
    int type = deviceType_.load(std::memory_order_relaxed);
    if (type == kDeviceTypeMicrophone) {
        options_.microphoneReferenceDbfs = value;
    } else {
        options_.monitorReferenceDbfs = value;
    }
    options_.referenceDbfsOverride = true; // Enable custom reference level
    saveReferenceLevels();
}

double AudioCapture::microphoneReferenceDbfs() const { return options_.microphoneReferenceDbfs; }

double AudioCapture::monitorReferenceDbfs() const { return options_.monitorReferenceDbfs; }

void AudioCapture::setMicrophoneReferenceDbfs(double value) {
    options_.microphoneReferenceDbfs = value;
    options_.referenceDbfsOverride = true; // Enable custom reference level
    saveReferenceLevels();
}

void AudioCapture::setMonitorReferenceDbfs(double value) {
    options_.monitorReferenceDbfs = value;
    options_.referenceDbfsOverride = true; // Enable custom reference level
    saveReferenceLevels();
}

double AudioCapture::effectiveReferenceDbfs() const {
    int type = deviceType_.load(std::memory_order_relaxed);
    if (type == kDeviceTypeMicrophone) {
        // Microphone mode - use per-device microphone reference
        return options_.microphoneReferenceDbfs;
    } else {
        // System output mode - use per-device monitor reference
        return options_.monitorReferenceDbfs;
    }
}

float AudioCapture::leftVuDb() const { return leftVuDb_.load(std::memory_order_relaxed); }

float AudioCapture::rightVuDb() const { return rightVuDb_.load(std::memory_order_relaxed); }

void AudioCapture::loadReferenceLevels() {
    QSettings settings;
    settings.beginGroup("AudioCapture");
    options_.microphoneReferenceDbfs = settings.value("microphoneReferenceDbfs", 0.0).toDouble();
    options_.monitorReferenceDbfs = settings.value("monitorReferenceDbfs", -14.0).toDouble();
    settings.endGroup();
}

void AudioCapture::saveReferenceLevels() {
    QSettings settings;
    settings.beginGroup("AudioCapture");
    settings.setValue("microphoneReferenceDbfs", options_.microphoneReferenceDbfs);
    settings.setValue("monitorReferenceDbfs", options_.monitorReferenceDbfs);
    settings.endGroup();
}

// -------- Helper Functions --------

pa_context* AudioCapture::create_temporary_context(pa_mainloop*& ml) {
    ml = pa_mainloop_new();
    if (!ml) {
        return nullptr;
    }

    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "VU Meter Device List");
    if (!ctx) {
        pa_mainloop_free(ml);
        ml = nullptr;
        return nullptr;
    }

    bool ready = false;
    int ret = 0;

    auto ctx_state_cb = [](pa_context* c, void* userdata) {
        auto* flag = static_cast<bool*>(userdata);
        pa_context_state_t st = pa_context_get_state(c);
        if (st == PA_CONTEXT_READY || st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) {
            *flag = true;
        }
    };

    pa_context_set_state_callback(ctx, ctx_state_cb, &ready);
    pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    while (!ready) {
        pa_mainloop_iterate(ml, 1, &ret);
    }

    if (pa_context_get_state(ctx) != PA_CONTEXT_READY) {
        // Don't disconnect - context is already FAILED or TERMINATED
        // Calling pa_context_disconnect() on a failed context causes segfault
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        ml = nullptr;
        return nullptr;
    }

    return ctx;
}

void AudioCapture::create_stream_from_spec(const pa_sample_spec& sample_spec,
                                           const pa_channel_map& channel_map,
                                           const char* source_name) {
    pa_sample_spec nss = sample_spec;
    nss.format = PA_SAMPLE_FLOAT32;

    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_FILTER_APPLY, "echo-cancel noise-suppression=0 aec=0 agc=0");

    stream_ = pa_stream_new_with_proplist(context_, "VU Meter Capture", &nss, &channel_map, props);
    pa_proplist_free(props);

    if (!stream_) {
        emit errorOccurred(QStringLiteral("Failed to create PulseAudio stream"));
        return;
    }

    pa_stream_set_state_callback(stream_, &AudioCapture::stream_state_callback, this);
    pa_stream_set_read_callback(stream_, &AudioCapture::stream_read_callback, this);

    pa_buffer_attr attr;
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.tlength = static_cast<uint32_t>(-1);
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);
    attr.fragsize = options_.sampleRate / (1000 / kFragmentSizeMs);

    int ret = pa_stream_connect_record(stream_, source_name, &attr, PA_STREAM_ADJUST_LATENCY);
    if (ret < 0) {
        emit errorOccurred(QStringLiteral("Failed to connect stream to %1").arg(source_name));
    }
}

// -------- Device Enumeration --------

QList<AudioCapture::DeviceInfo> AudioCapture::enumerateInputDevices() {
    QList<DeviceInfo> result;

    // Temporary mainloop + context for enumeration
    pa_mainloop* ml = nullptr;
    pa_context* ctx = create_temporary_context(ml);
    if (!ctx) {
        return result;
    }

    // Get default source and sink
    QString defaultSource;
    QString defaultSink;

    auto server_cb = [](pa_context* /*c*/, const pa_server_info* info, void* userdata) {
        auto* pair = static_cast<QPair<QString, QString>*>(userdata);
        if (info) {
            pair->first = QString::fromUtf8(info->default_source_name);
            pair->second = QString::fromUtf8(info->default_sink_name);
        }
    };

    QPair<QString, QString> defaults;
    int ret = 0;
    pa_operation* op0 = pa_context_get_server_info(ctx, server_cb, &defaults);
    while (pa_operation_get_state(op0) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(ml, 1, &ret);
    }
    pa_operation_unref(op0);

    defaultSource = defaults.first;
    defaultSink = defaults.second;
    QString defaultMonitorSource = defaultSink + ".monitor";

    // Enumerate sources
    struct SourceListContext {
        QList<DeviceInfo>* result;
        QString defaultSource;
        QString defaultMonitorSource;
    };

    auto source_cb = [](pa_context* /*c*/, const pa_source_info* info, int eol, void* userdata) {
        if (eol > 0 || !info) {
            return;
        }

        auto* ctx = static_cast<SourceListContext*>(userdata);

        DeviceInfo device;
        device.name = QString::fromUtf8(info->description);
        device.uid = QString::fromUtf8(info->name);
        device.channels = static_cast<int>(info->sample_spec.channels);
        device.isInput = true;

        // Mark as default - prioritize monitor source since that's our actual default
        QString deviceName = QString::fromUtf8(info->name);
        if (deviceName == ctx->defaultMonitorSource) {
            device.isDefault = true;
        } else if (deviceName == ctx->defaultSource) {
            // Only mark as default if monitor source doesn't exist
            bool monitorExists = false;
            for (const auto& existingDevice : *ctx->result) {
                if (existingDevice.uid == ctx->defaultMonitorSource) {
                    monitorExists = true;
                    break;
                }
            }
            device.isDefault = !monitorExists;
        } else {
            device.isDefault = false;
        }

        ctx->result->append(device);
    };

    SourceListContext sourceCtx{&result, defaultSource, defaultMonitorSource};
    pa_operation* op1 = pa_context_get_source_info_list(ctx, source_cb, &sourceCtx);
    while (pa_operation_get_state(op1) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(ml, 1, &ret);
    }
    pa_operation_unref(op1);

    // Cleanup - don't disconnect temporary contexts, just unref
    pa_context_unref(ctx);
    pa_mainloop_free(ml);

    return result;
}

QString AudioCapture::listDevicesString() {
    QString out;
    out += "PulseAudio devices:\n\n";

    // Temporary mainloop + context for enumeration
    pa_mainloop* ml = nullptr;
    pa_context* ctx = create_temporary_context(ml);
    if (!ctx) {
        return "Failed to create PulseAudio context\n";
    }

    // Get default sink/source
    QString defaultSink;
    QString defaultSource;

    auto server_cb = [](pa_context* /*c*/, const pa_server_info* info, void* userdata) {
        auto* pair = static_cast<QPair<QString, QString>*>(userdata);
        if (info) {
            pair->first = QString::fromUtf8(info->default_sink_name);
            pair->second = QString::fromUtf8(info->default_source_name);
        }
    };

    QPair<QString, QString> defaults;
    int ret = 0;
    pa_operation* op0 = pa_context_get_server_info(ctx, server_cb, &defaults);
    while (pa_operation_get_state(op0) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(ml, 1, &ret);
    }
    pa_operation_unref(op0);

    defaultSink = defaults.first;
    defaultSource = defaults.second;

    // Context structs for callbacks
    struct SinkListContext {
        QString* out;
        QString defaultName;
    };

    struct SourceListContext {
        QString* out;
        QString defaultName;
    };

    // Enumerate sinks
    QString sinks;

    auto sink_cb = [](pa_context* /*c*/, const pa_sink_info* info, int eol, void* userdata) {
        if (eol > 0 || !info) {
            return;
        }

        auto* ctx = static_cast<SinkListContext*>(userdata);
        bool isDefault = (QString::fromUtf8(info->name) == ctx->defaultName);

        ctx->out->append(
            QString("Sink: %1%2\n").arg(QString::fromUtf8(info->name)).arg(isDefault ? "   [DEFAULT]" : ""));
        ctx->out->append(QString("  Description: %1\n").arg(QString::fromUtf8(info->description)));
        ctx->out->append(QString("  Monitor source: %1\n\n").arg(QString::fromUtf8(info->monitor_source_name)));
    };

    SinkListContext sinkCtx{&sinks, defaultSink};
    pa_operation* op1 = pa_context_get_sink_info_list(ctx, sink_cb, &sinkCtx);
    while (pa_operation_get_state(op1) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(ml, 1, &ret);
    }
    pa_operation_unref(op1);

    // Enumerate sources
    QString sources;

    auto source_cb = [](pa_context* /*c*/, const pa_source_info* info, int eol, void* userdata) {
        if (eol > 0 || !info) {
            return;
        }

        auto* ctx = static_cast<SourceListContext*>(userdata);
        bool isDefault = (QString::fromUtf8(info->name) == ctx->defaultName);

        ctx->out->append(
            QString("Source: %1%2\n").arg(QString::fromUtf8(info->name)).arg(isDefault ? "   [DEFAULT]" : ""));
        ctx->out->append(QString("  Description: %1\n\n").arg(QString::fromUtf8(info->description)));
    };

    SourceListContext sourceCtx{&sources, defaultSource};
    pa_operation* op2 = pa_context_get_source_info_list(ctx, source_cb, &sourceCtx);
    while (pa_operation_get_state(op2) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(ml, 1, &ret);
    }
    pa_operation_unref(op2);

    // Cleanup - don't disconnect temporary contexts, just unref
    pa_context_unref(ctx);
    pa_mainloop_free(ml);

    // Format output
    out += "=== Output Sinks ===\n";
    out += sinks;
    out += "=== Input Sources ===\n";
    out += sources;

    out += "\nUsage:\n";
    out += "  --device-type 0   Use system output (sink monitor)\n";
    out += "  --device-type 1   Use microphone input (source)\n";
    out += "  --device-name <n>   Use specific sink or source\n";

    return out;
}

// -------- PulseAudio Callbacks --------

void AudioCapture::stream_read_callback(pa_stream* s, size_t length, void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);
    const void* p = nullptr;

    if (pa_stream_peek(s, &p, &length) < 0 || !p || length == 0) {
        return;
    }

    const float* data = static_cast<const float*>(p);
    const pa_sample_spec* ss = pa_stream_get_sample_spec(s);
    if (!ss || ss->channels < 1) {
        pa_stream_drop(s);
        return;
    }

    const unsigned int channels = ss->channels;
    const unsigned int samples = static_cast<unsigned int>(length / sizeof(float));
    const unsigned int frames = samples / channels;
    if (frames == 0) {
        pa_stream_drop(s);
        return;
    }

    // Use per-instance DSP state (FIXED: was static!)
    VuReferenceOptions ref;

    // Read the appropriate per-device reference level based on device type
    int deviceType = self->deviceType_.load(std::memory_order_relaxed);
    if (deviceType == kDeviceTypeMicrophone) {
        ref.referenceDbfs = self->options_.microphoneReferenceDbfs;
    } else {
        ref.referenceDbfs = self->options_.monitorReferenceDbfs;
    }

    ref.referenceDbfsOverride = self->options_.referenceDbfsOverride;
    ref.deviceType = deviceType;

    float vuL = kAudioFloorVu;
    float vuR = kAudioFloorVu;
    processInterleavedFloatAudioToVuDb(data,
                                       frames,
                                       channels,
                                       static_cast<float>(ss->rate),
                                       ref,
                                       self->ballisticsL_,
                                       self->ballisticsR_,
                                       *self->dspState_, // Use instance member, not static!
                                       kAudioFloorVu,
                                       kAudioCeilingVu,
                                       vuL,
                                       vuR);

    self->leftVuDb_.store(vuL, std::memory_order_relaxed);
    self->rightVuDb_.store(vuR, std::memory_order_relaxed);

    pa_stream_drop(s);
}

void AudioCapture::stream_state_callback(pa_stream* s, void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY:
        break;
    case PA_STREAM_FAILED:
        emit self->errorOccurred(QStringLiteral("PulseAudio stream failed"));
        break;
    default:
        break;
    }
}

void AudioCapture::sink_info_callback(pa_context* /*c*/, const pa_sink_info* si, int is_last, void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    if (is_last < 0 || !si) {
        emit self->errorOccurred(QStringLiteral("Failed to get sink info"));
        return;
    }

    if (is_last > 0) {
        return;
    }

    // Use the helper function to reduce duplication
    self->create_stream_from_spec(si->sample_spec, si->channel_map, si->monitor_source_name);

    // Update current device UID
    self->currentDeviceUID_ = QString::fromUtf8(si->monitor_source_name);
}

void AudioCapture::source_info_callback(pa_context* /*c*/, const pa_source_info* si, int is_last, void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    if (is_last < 0 || !si) {
        emit self->errorOccurred(QStringLiteral("Failed to get source info"));
        return;
    }

    if (is_last > 0) {
        return;
    }

    // Use the helper function to reduce duplication
    self->create_stream_from_spec(si->sample_spec, si->channel_map, si->name);

    // Update current device UID
    self->currentDeviceUID_ = QString::fromUtf8(si->name);
}

void AudioCapture::context_state_callback(pa_context* c, void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        const char* name = nullptr;
        QByteArray utf8;
        if (!self->options_.deviceName.isEmpty()) {
            utf8 = self->options_.deviceName.toUtf8();
            name = utf8.constData();
        }

        pa_operation* op = nullptr;
        int type = self->deviceType_.load(std::memory_order_relaxed);

        if (type == kDeviceTypeMicrophone) {
            // Source (mic)
            if (name) {
                op = pa_context_get_source_info_by_name(c, name, &AudioCapture::source_info_callback, self);
            } else {
                op = pa_context_get_source_info_list(c, &AudioCapture::source_info_callback, self);
            }
        } else {
            // Sink monitor (system output)
            if (name) {
                op = pa_context_get_sink_info_by_name(c, name, &AudioCapture::sink_info_callback, self);
            } else {
                op = pa_context_get_sink_info_list(c, &AudioCapture::sink_info_callback, self);
            }
        }
        if (op) {
            pa_operation_unref(op);
        }
        break;
    }
    case PA_CONTEXT_FAILED:
        emit self->errorOccurred(QStringLiteral("PulseAudio context failed"));
        break;
    default:
        break;
    }
}

#endif // __linux__
