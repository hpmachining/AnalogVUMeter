#include "AudioCapture.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include <QByteArray>
#include <QPair>
#include <pulse/pulseaudio.h>

static constexpr float kMinVu = -22.0f;
static constexpr float kMaxVu = 3.0f;

AudioCapture::AudioCapture(const Options& options, QObject* parent)
    : QObject(parent), options_(options), ballisticsL_(kMinVu), ballisticsR_(kMinVu) {}

AudioCapture::~AudioCapture() { stop(); }

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

    // Start the PulseAudio mainloop in a separate thread, but with simple synchronization
    thread_ = std::thread([this]() {
        int ret = 0;
        pa_mainloop_run(mainloop_, &ret);
    });

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
        pa_context_disconnect(context_);
        pa_context_unref(context_);
        context_ = nullptr;
    }
    if (mainloop_) {
        pa_mainloop_free(mainloop_);
        mainloop_ = nullptr;
    }
}

float AudioCapture::leftVuDb() const { return leftVuDb_.load(std::memory_order_relaxed); }

float AudioCapture::rightVuDb() const { return rightVuDb_.load(std::memory_order_relaxed); }

QString AudioCapture::listDevicesString() {
    QString out;
    out += "PulseAudio devices:\n\n";

    // Temporary mainloop + context for enumeration
    pa_mainloop* ml = pa_mainloop_new();
    if (!ml)
        return "Failed to create PulseAudio mainloop\n";

    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "VU Meter Device List");
    if (!ctx) {
        pa_mainloop_free(ml);
        return "Failed to create PulseAudio context\n";
    }

    bool ready = false;
    int ret = 0;

    // --- Context state callback ---
    auto ctx_state_cb = [](pa_context* c, void* userdata) {
        auto* flag = static_cast<bool*>(userdata);
        pa_context_state_t st = pa_context_get_state(c);
        if (st == PA_CONTEXT_READY || st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)
            *flag = true;
    };

    pa_context_set_state_callback(ctx, ctx_state_cb, &ready);
    pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    while (!ready)
        pa_mainloop_iterate(ml, 1, &ret);

    if (pa_context_get_state(ctx) != PA_CONTEXT_READY) {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return "PulseAudio context failed\n";
    }

    // --- Get default sink/source ---
    QString defaultSink;
    QString defaultSource;

    auto server_cb = [](pa_context*, const pa_server_info* info, void* userdata) {
        auto* pair = static_cast<QPair<QString, QString>*>(userdata);
        pair->first = QString::fromUtf8(info->default_sink_name);
        pair->second = QString::fromUtf8(info->default_source_name);
    };

    QPair<QString, QString> defaults;
    pa_operation* op0 = pa_context_get_server_info(ctx, server_cb, &defaults);
    while (pa_operation_get_state(op0) == PA_OPERATION_RUNNING)
        pa_mainloop_iterate(ml, 1, &ret);
    pa_operation_unref(op0);

    defaultSink = defaults.first;
    defaultSource = defaults.second;

    // --- Context structs for callbacks ---
    struct SinkListContext {
        QString* out;
        QString defaultName;
    };

    struct SourceListContext {
        QString* out;
        QString defaultName;
    };

    // --- Enumerate sinks ---
    QString sinks;

    auto sink_cb = [](pa_context*, const pa_sink_info* info, int eol, void* userdata) {
        if (eol > 0 || !info)
            return;

        auto* ctx = static_cast<SinkListContext*>(userdata);

        bool isDefault = (QString::fromUtf8(info->name) == ctx->defaultName);

        ctx->out->append(
            QString("Sink: %1%2\n").arg(QString::fromUtf8(info->name)).arg(isDefault ? "   [DEFAULT]" : ""));
        ctx->out->append(QString("  Description: %1\n").arg(QString::fromUtf8(info->description)));
        ctx->out->append(QString("  Monitor source: %1\n\n").arg(QString::fromUtf8(info->monitor_source_name)));
    };

    SinkListContext sinkCtx{&sinks, defaultSink};

    pa_operation* op1 = pa_context_get_sink_info_list(ctx, sink_cb, &sinkCtx);
    while (pa_operation_get_state(op1) == PA_OPERATION_RUNNING)
        pa_mainloop_iterate(ml, 1, &ret);
    pa_operation_unref(op1);

    // --- Enumerate sources ---
    QString sources;

    auto source_cb = [](pa_context*, const pa_source_info* info, int eol, void* userdata) {
        if (eol > 0 || !info)
            return;

        auto* ctx = static_cast<SourceListContext*>(userdata);

        bool isDefault = (QString::fromUtf8(info->name) == ctx->defaultName);

        ctx->out->append(
            QString("Source: %1%2\n").arg(QString::fromUtf8(info->name)).arg(isDefault ? "   [DEFAULT]" : ""));
        ctx->out->append(QString("  Description: %1\n\n").arg(QString::fromUtf8(info->description)));
    };

    SourceListContext sourceCtx{&sources, defaultSource};

    pa_operation* op2 = pa_context_get_source_info_list(ctx, source_cb, &sourceCtx);
    while (pa_operation_get_state(op2) == PA_OPERATION_RUNNING)
        pa_mainloop_iterate(ml, 1, &ret);
    pa_operation_unref(op2);

    // Cleanup
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);

    // --- Format output ---
    out += "=== Output Sinks ===\n";
    out += sinks;
    out += "=== Input Sources ===\n";
    out += sources;

    out += "\nUsage:\n";
    out += "  --device-type 0   Use system output (sink monitor)\n";
    out += "  --device-type 1   Use microphone input (source)\n";
    out += "  --device-name <name>   Use specific sink or source\n";

    return out;
}

// -------- PulseAudio callbacks --------

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

    // --- Compute raw RMS for this buffer ---
    double sumL = 0.0;
    double sumR = 0.0;

    static float prevL = 0.0f;
    static float prevR = 0.0f;
    for (unsigned int i = 0; i < frames; ++i) {
        const float rawL = data[i * channels + 0];
        const float rawR = (channels > 1) ? data[i * channels + 1] : rawL;

        // Transient pre-emphasis (very subtle)
        const float l = rawL + 0.15f * (rawL - prevL);
        const float r = rawR + 0.15f * (rawR - prevR);

        prevL = rawL;
        prevR = rawR;

        sumL += static_cast<double>(l) * static_cast<double>(l);
        sumR += static_cast<double>(r) * static_cast<double>(r);
    }

    float rmsL = std::sqrt(static_cast<float>(sumL / frames));
    float rmsR = std::sqrt(static_cast<float>(sumR / frames));

    // --- Vintage VU RMS integration (250 ms) ---
    static float rmsL_smooth = 0.0f;
    static float rmsR_smooth = 0.0f;

    const float wakeThreshold = 0.002f; // about -54 dBFS

    if (rmsL > wakeThreshold) {
        rmsL_smooth = rmsL * rmsL;
    }
    if (rmsR > wakeThreshold) {
        rmsR_smooth = rmsR * rmsR;
    }

    float vuTau = 0.020f;
    float dt = static_cast<float>(frames) / static_cast<float>(ss->rate);
    dt = std::min(dt, 0.050f); // clamp to 50 ms
    const float alpha = std::exp(-dt / vuTau);

    rmsL_smooth = alpha * rmsL_smooth + (1.0f - alpha) * (rmsL * rmsL);
    rmsR_smooth = alpha * rmsR_smooth + (1.0f - alpha) * (rmsR * rmsR);

    float rmsL_vu = std::sqrt(rmsL_smooth);
    float rmsR_vu = std::sqrt(rmsR_smooth);

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
    if (self->options_.referenceDbfsOverride) {
        effectiveRefDbfs = static_cast<float>(self->options_.referenceDbfs);
    } else if (self->options_.deviceType == 1) {
        // Microphone mode
        effectiveRefDbfs = -0.0f;
    } else {
        // System output mode
        effectiveRefDbfs = -14.0f;
    }

    float targetVuL = dbfsL - effectiveRefDbfs;
    float targetVuR = dbfsR - effectiveRefDbfs;

    static bool meterAwake = false;
    if (!meterAwake && (rmsL_vu > 0.002f || rmsR_vu > 0.002f)) {
        self->ballisticsL_.reset(targetVuL);
        self->ballisticsR_.reset(targetVuR);
        meterAwake = true;
    }

    // --- Apply ballistics using per-callback dt ---
    float vuL = self->ballisticsL_.process(targetVuL, dt);
    float vuR = self->ballisticsR_.process(targetVuR, dt);

    // --- Clamp to meter scale ---
    vuL = std::clamp(vuL, kMinVu, kMaxVu);
    vuR = std::clamp(vuR, kMinVu, kMaxVu);

    self->leftVuDb_.store(vuL, std::memory_order_relaxed);
    self->rightVuDb_.store(vuR, std::memory_order_relaxed);

    pa_stream_drop(s);
}

void AudioCapture::stream_state_callback(pa_stream* s, void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY: {
        break;
    }
    case PA_STREAM_FAILED: {
        emit self->errorOccurred(QStringLiteral("PulseAudio stream failed"));
        break;
    }
    default:
        break;
    }
}

void AudioCapture::sink_info_callback(pa_context* c, const pa_sink_info* si, int is_last, void* userdata) {
    (void)c;
    auto* self = static_cast<AudioCapture*>(userdata);

    if (is_last < 0 || !si) {
        emit self->errorOccurred(QStringLiteral("Failed to get sink info"));
        return;
    }

    if (is_last > 0) {
        return;
    }

    pa_sample_spec nss = si->sample_spec;
    nss.format = PA_SAMPLE_FLOAT32;

    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_FILTER_APPLY, "echo-cancel noise-suppression=0 aec=0 agc=0");

    self->stream_ = pa_stream_new_with_proplist(self->context_, "VU Meter Capture", &nss, &si->channel_map, props);

    pa_proplist_free(props);
    pa_stream_set_state_callback(self->stream_, &AudioCapture::stream_state_callback, self);
    pa_stream_set_read_callback(self->stream_, &AudioCapture::stream_read_callback, self);

    pa_buffer_attr attr;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.fragsize = self->options_.sampleRate / 100; // ~10 ms

    pa_stream_connect_record(self->stream_, si->monitor_source_name, &attr, PA_STREAM_ADJUST_LATENCY);
}

void AudioCapture::source_info_callback(pa_context* c, const pa_source_info* si, int is_last, void* userdata) {
    (void)c;
    auto* self = static_cast<AudioCapture*>(userdata);

    if (is_last < 0 || !si) {
        emit self->errorOccurred(QStringLiteral("Failed to get source info"));
        return;
    }

    if (is_last > 0) {
        return;
    }

    pa_sample_spec nss = si->sample_spec;
    nss.format = PA_SAMPLE_FLOAT32;

    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_FILTER_APPLY, "echo-cancel noise-suppression=0 aec=0 agc=0");

    self->stream_ = pa_stream_new_with_proplist(self->context_, "VU Meter Capture", &nss, &si->channel_map, props);

    pa_proplist_free(props);
    pa_stream_set_state_callback(self->stream_, &AudioCapture::stream_state_callback, self);
    pa_stream_set_read_callback(self->stream_, &AudioCapture::stream_read_callback, self);

    pa_buffer_attr attr;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.fragsize = self->options_.sampleRate / 100; // ~10 ms

    pa_stream_connect_record(self->stream_, si->name, &attr, PA_STREAM_ADJUST_LATENCY);
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
        if (self->options_.deviceType == 1) {
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
        if (op)
            pa_operation_unref(op);
        break;
    }
    case PA_CONTEXT_FAILED: {
        emit self->errorOccurred(QStringLiteral("PulseAudio context failed"));
        break;
    }
    default:
        break;
    }
}
