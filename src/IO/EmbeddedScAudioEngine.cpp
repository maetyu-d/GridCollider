#include "EmbeddedScAudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <type_traits>

#if GRIDCOLLIDER_HAS_SC_HEADERS
#include <SC_WorldOptions.h>
#endif

#if JUCE_MAC || JUCE_LINUX
#include <dlfcn.h>
#endif

#if JUCE_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif

namespace gridcollider
{
namespace
{
constexpr int hostRenderQuantum = 64;
constexpr int sourceGroupId = 1;
constexpr int masterGroupId = 2;
constexpr int masterNodeId = 1000;
constexpr int masterBus = 16;
constexpr float outputLimit = 0.98f;

struct OscArgument
{
    enum class Type { integer, floating, string, blob };

    static OscArgument integer(const std::int32_t value) { return { Type::integer, value, 0.0f, {}, {} }; }
    static OscArgument floating(const float value) { return { Type::floating, 0, value, {}, {} }; }
    static OscArgument string(juce::String value) { return { Type::string, 0, 0.0f, std::move(value), {} }; }
    static OscArgument blob(std::vector<char> value) { return { Type::blob, 0, 0.0f, {}, std::move(value) }; }

    Type type;
    std::int32_t intValue = 0;
    float floatValue = 0.0f;
    juce::String stringValue;
    std::vector<char> blobValue;
};

void appendPaddedString(std::vector<char>& packet, const juce::String& value)
{
    const auto utf8 = value.toStdString();
    packet.insert(packet.end(), utf8.begin(), utf8.end());
    packet.push_back('\0');

    while ((packet.size() % 4) != 0)
        packet.push_back('\0');
}

void appendInt32(std::vector<char>& packet, const std::int32_t value)
{
    packet.push_back(static_cast<char>((value >> 24) & 0xff));
    packet.push_back(static_cast<char>((value >> 16) & 0xff));
    packet.push_back(static_cast<char>((value >> 8) & 0xff));
    packet.push_back(static_cast<char>(value & 0xff));
}

void appendFloat32(std::vector<char>& packet, const float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendInt32(packet, static_cast<std::int32_t>(bits));
}

void appendBlob(std::vector<char>& packet, const std::vector<char>& blob)
{
    appendInt32(packet, static_cast<std::int32_t>(blob.size()));
    packet.insert(packet.end(), blob.begin(), blob.end());

    while ((packet.size() % 4) != 0)
        packet.push_back('\0');
}

std::vector<char> buildOscMessage(const juce::String& address, const std::vector<OscArgument>& arguments)
{
    std::vector<char> packet;
    appendPaddedString(packet, address);

    juce::String tags = ",";
    for (const auto& argument : arguments)
    {
        switch (argument.type)
        {
            case OscArgument::Type::integer:  tags << "i"; break;
            case OscArgument::Type::floating: tags << "f"; break;
            case OscArgument::Type::string:   tags << "s"; break;
            case OscArgument::Type::blob:     tags << "b"; break;
        }
    }

    appendPaddedString(packet, tags);

    for (const auto& argument : arguments)
    {
        switch (argument.type)
        {
            case OscArgument::Type::integer:  appendInt32(packet, argument.intValue); break;
            case OscArgument::Type::floating: appendFloat32(packet, argument.floatValue); break;
            case OscArgument::Type::string:   appendPaddedString(packet, argument.stringValue); break;
            case OscArgument::Type::blob:     appendBlob(packet, argument.blobValue); break;
        }
    }

    return packet;
}

juce::String midiToFreqExpression()
{
    return "freq = pitch.midicps;";
}

juce::String starterSynthSource(const juce::String& name)
{
    if (name == "kick")
        return "SynthDef(\\kick, { |out = 0, pitch = 36, amp = 0.7, sustain = 0.25, pan = 0| var env, freq, sig; env = EnvGen.kr(Env.perc(0.001, sustain), doneAction: 2); freq = EnvGen.kr(Env([pitch.midicps * 2.4, pitch.midicps], [0.05], -4)); sig = SinOsc.ar(freq) * env * amp; Out.ar(out, Pan2.ar((sig * 2.5).tanh, pan)); })";

    if (name == "snare")
        return "SynthDef(\\snare, { |out = 0, pitch = 60, amp = 0.45, sustain = 0.18, pan = 0| var env, sig; env = EnvGen.kr(Env.perc(0.001, sustain), doneAction: 2); sig = HPF.ar(WhiteNoise.ar, 1500) * env * amp; Out.ar(out, Pan2.ar(sig.tanh, pan)); })";

    if (name == "hat")
        return "SynthDef(\\hat, { |out = 0, pitch = 80, amp = 0.22, sustain = 0.06, pan = 0| var env, sig; env = EnvGen.kr(Env.perc(0.001, sustain), doneAction: 2); sig = HPF.ar(WhiteNoise.ar, pitch.midicps.max(4000)) * env * amp; Out.ar(out, Pan2.ar(sig, pan)); })";

    if (name == "bass")
        return "SynthDef(\\bass, { |out = 0, pitch = 36, amp = 0.35, sustain = 0.35, pan = 0| var freq, env, sig; " + midiToFreqExpression() + " env = EnvGen.kr(Env.perc(0.005, sustain), doneAction: 2); sig = LPF.ar(Saw.ar(freq) * env, freq * 6) * amp; Out.ar(out, Pan2.ar(sig.tanh, pan)); })";

    if (name == "grain")
        return "SynthDef(\\grain, { |out = 0, pitch = 60, amp = 0.20, sustain = 0.65, pan = 0| var freq, env, sig; " + midiToFreqExpression() + " env = EnvGen.kr(Env.perc(0.01, sustain), doneAction: 2); sig = SinOsc.ar(freq * LFNoise1.kr(18).range(0.7, 1.6)) * env * amp; Out.ar(out, Pan2.ar(sig, pan)); })";

    if (name == "drone")
        return "SynthDef(\\drone, { |out = 0, pitch = 48, amp = 0.18, sustain = 2.8, pan = 0| var freq, env, sig; " + midiToFreqExpression() + " env = EnvGen.kr(Env.linen(0.08, sustain, 0.6), doneAction: 2); sig = Mix(Saw.ar(freq * [0.5, 1, 1.005])) * 0.25; sig = RLPF.ar(sig, freq * 5, 0.35) * env * amp; Out.ar(out, Pan2.ar(sig, pan)); })";

    return "SynthDef(\\tone, { |out = 0, pitch = 60, amp = 0.25, sustain = 0.25, pan = 0| var freq, env, sig; " + midiToFreqExpression() + " env = EnvGen.kr(Env.perc(0.004, sustain), doneAction: 2); sig = SinOsc.ar(freq) * env * amp; Out.ar(out, Pan2.ar(sig, pan)); })";
}

juce::String masterSynthSource()
{
    return "SynthDef(\\gcMaster, { |inBus = 16, out = 0, master = 0.9, drive = 0.88| var sig; sig = In.ar(inBus, 2); sig = LeakDC.ar(sig); sig = (sig * drive).tanh; sig = Limiter.ar(sig, 0.96, 0.02) * master; Out.ar(out, sig); })";
}

void debugLog(const juce::String& message)
{
    std::fprintf(stderr, "[GridCollider SC] %s\n", message.toRawUTF8());
    std::fflush(stderr);
}

juce::String instrumentFor(const juce::String& name)
{
    if (name == "mono")
        return "bass";
    if (name == "midi")
        return "tone";
    if (name == "kick" || name == "snare" || name == "hat" || name == "bass" || name == "tone" || name == "grain" || name == "drone")
        return name;
    return "tone";
}

float defaultLevelFor(const juce::String& instrument)
{
    if (instrument == "kick")  return 0.95f;
    if (instrument == "snare") return 0.78f;
    if (instrument == "hat")   return 0.55f;
    if (instrument == "bass")  return 0.82f;
    if (instrument == "grain") return 0.62f;
    if (instrument == "drone") return 0.50f;
    return 0.68f;
}

juce::String normalisedControlParameter(const juce::String& name)
{
    const auto lower = name.toLowerCase();

    if (lower == "level" || lower == "volume" || lower == "amp" || lower == "gain" || lower == "cc7")
        return "level";
    if (lower == "pan" || lower == "cc10")
        return "pan";
    if (lower == "drive" || lower == "cc74")
        return "drive";
    if (lower == "master")
        return "master";

    return lower;
}

float panForX(const int x)
{
    return juce::jlimit(-0.9f, 0.9f, (static_cast<float>(x) / 63.0f) * 1.8f - 0.9f);
}

float secondsForTicks(const std::uint64_t ticks, const double bpm)
{
    const auto safeBpm = juce::jlimit(1.0, 999.0, std::isfinite(bpm) ? bpm : 120.0);
    return static_cast<float>(juce::jmax(1.0, static_cast<double>(ticks)) * 60.0 / safeBpm);
}

juce::StringArray libraryCandidates(const char* envName, const juce::String& relativeFromAlchemy, const juce::String& fileName)
{
    juce::StringArray candidates;

    if (const auto* env = std::getenv(envName))
        candidates.addIfNotAlreadyThere(env);

    const juce::File alchemyRoot(GRIDCOLLIDER_ALCHEMY_SC_ROOT);
    candidates.addIfNotAlreadyThere(alchemyRoot.getChildFile(relativeFromAlchemy).getFullPathName());
    candidates.addIfNotAlreadyThere(fileName);
    return candidates;
}
}

struct EmbeddedScAudioEngine::Impl
{
#if GRIDCOLLIDER_HAS_SC_HEADERS
    using WorldNewFn = World* (*) (WorldOptions*);
    using WorldCleanupFn = void (*) (World*, bool);
    using WorldSendPacketFn = bool (*) (World*, int, char*, ReplyFunc);
    using WorldRenderHostAudioFn = bool (*) (World*, const float*, float*, int, int, int);
    using CompileSynthDefFn = bool (*) (const char*, const char*, const char*, char*, int);
    using InitialiseLangFn = bool (*) (const char*, char*, int);

    ~Impl() { release(); }

    bool prepare(const double sampleRate, const int maximumBlockSize, const int outputChannels)
    {
        const juce::ScopedLock lock(engineLock);
        releaseUnlocked();

        if (! loadServerApi() || ! loadLanguageApi())
            return false;

        currentSampleRate = sampleRate;
        maxBlockSize = juce::jmax(maximumBlockSize, hostRenderQuantum);
        numOutputChannels = juce::jlimit(1, 8, outputChannels);
        interleavedOutput.assign(static_cast<std::size_t>(maxBlockSize + hostRenderQuantum) * static_cast<std::size_t>(numOutputChannels), 0.0f);

        worldOptions = WorldOptions();
        worldOptions.mRealTime = true;
        worldOptions.mRendezvous = false;
        worldOptions.mVerbosity = -1;
        worldOptions.mLoadGraphDefs = 0;
        worldOptions.mPreferredSampleRate = static_cast<uint32>(sampleRate);
        worldOptions.mPreferredHardwareBufferFrameSize = hostRenderQuantum;
        worldOptions.mBufLength = hostRenderQuantum;
        worldOptions.mNumInputBusChannels = 0;
        worldOptions.mNumOutputBusChannels = static_cast<uint32>(numOutputChannels);
        worldOptions.mNumAudioBusChannels = 1024;
        pluginPath = findPluginPath();
        worldOptions.mUGensPluginPath = pluginPath.isNotEmpty() ? pluginPath.toRawUTF8() : nullptr;

        world = worldNew(&worldOptions);
        if (world == nullptr)
        {
            lastError = "Embedded SuperCollider world could not start";
            releaseUnlocked();
            return false;
        }

        sendPacket(buildOscMessage("/g_new", { OscArgument::integer(sourceGroupId), OscArgument::integer(0), OscArgument::integer(0) }));
        sendPacket(buildOscMessage("/g_new", { OscArgument::integer(masterGroupId), OscArgument::integer(1), OscArgument::integer(0) }));

        if (! loadStarterSynthDefs())
            return false;

        createMasterNode();

        ready.store(true, std::memory_order_release);
        status = "EMBEDDED SC READY";
        debugLog("ready at " + juce::String(sampleRate, 1) + " Hz, block " + juce::String(maximumBlockSize));
        lastError.clear();
        return true;
    }

    void release() noexcept
    {
        const juce::ScopedLock lock(engineLock);
        releaseUnlocked();
    }

    void render(juce::AudioBuffer<float>& output)
    {
        const juce::ScopedTryLock lock(engineLock);
        output.clear();

        if (! lock.isLocked() || ! ready.load(std::memory_order_acquire) || world == nullptr)
            return;

        const auto frames = output.getNumSamples();
        if (frames <= 0 || frames > maxBlockSize)
            return;

        flushQueuedEvents();

        const auto paddedFrames = ((frames + hostRenderQuantum - 1) / hostRenderQuantum) * hostRenderQuantum;
        std::fill(interleavedOutput.begin(), interleavedOutput.begin() + static_cast<std::ptrdiff_t>(paddedFrames * numOutputChannels), 0.0f);

        if (! worldRenderHostAudio(world, nullptr, interleavedOutput.data(), paddedFrames, 0, numOutputChannels))
        {
            renderFailures.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
        {
            auto* dst = output.getWritePointer(channel);
            const auto sourceChannel = juce::jmin(channel, numOutputChannels - 1);

            for (int frame = 0; frame < frames; ++frame)
            {
                const auto sample = interleavedOutput[static_cast<std::size_t>(frame * numOutputChannels + sourceChannel)];
                dst[frame] = std::isfinite(sample) ? juce::jlimit(-outputLimit, outputLimit, sample) : 0.0f;
            }
        }
    }

    void enqueue(const std::vector<InternalEvent>& events)
    {
        const juce::ScopedLock lock(queueLock);

        for (const auto& event : events)
        {
            std::visit([this](const auto& typed)
            {
                using Event = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Event, NoteEvent>)
                {
                    pendingPackets.push_back(packetFor(typed));
                    const auto queued = queuedEventCount.fetch_add(1, std::memory_order_relaxed);
                    if (queued < 16)
                        debugLog("queued note " + instrumentFor(typed.fields.instrumentName) + " pitch " + juce::String(typed.fields.pitch));
                }
                else if constexpr (std::is_same_v<Event, TriggerEvent>)
                {
                    pendingPackets.push_back(packetFor(typed));
                    const auto queued = queuedEventCount.fetch_add(1, std::memory_order_relaxed);
                    if (queued < 16)
                        debugLog("queued trigger " + typed.triggerName);
                }
                else if constexpr (std::is_same_v<Event, ControlEvent>)
                {
                    for (auto packet : packetsFor(typed))
                        pendingPackets.push_back(std::move(packet));

                    queuedEventCount.fetch_add(1, std::memory_order_relaxed);
                }
            }, event);
        }
    }

    void setTransport(const double bpmToUse, const std::uint64_t tickToUse, const bool playingToUse)
    {
        bpm.store(bpmToUse, std::memory_order_relaxed);
        tick.store(tickToUse, std::memory_order_relaxed);
        playing.store(playingToUse, std::memory_order_relaxed);
    }

    void setMasterLevel(const float level)
    {
        const auto clamped = juce::jlimit(0.0f, 1.25f, level);
        masterLevel = clamped;

        const juce::ScopedLock lock(queueLock);
        pendingPackets.push_back(buildOscMessage("/n_set",
                                                 { OscArgument::integer(masterNodeId),
                                                   OscArgument::string("master"),
                                                   OscArgument::floating(clamped) }));
    }

    bool loadSynthDef(const juce::String& name, const juce::String& source)
    {
        if (! ready.load(std::memory_order_acquire) || compileSynthDef == nullptr)
        {
            lastError = "Embedded SuperCollider is not ready";
            return false;
        }

        std::vector<char> bytes;

        {
            const juce::ScopedLock compileGuard(synthCompileLock);

            if (! ready.load(std::memory_order_acquire) || compileSynthDef == nullptr)
            {
                lastError = "Embedded SuperCollider is not ready";
                return false;
            }

            if (! compileSynth(name, source, bytes))
                return false;
        }

        {
            const juce::ScopedLock lock(engineLock);

            if (! ready.load(std::memory_order_acquire) || world == nullptr)
            {
                lastError = "Embedded SuperCollider is not ready";
                return false;
            }

            sendPacket(buildOscMessage("/d_recv", { OscArgument::blob(std::move(bytes)) }));
        }

        registerSynthName(name);
        debugLog("loaded SynthDef " + name);
        return true;
    }

    juce::String getStatusText() const
    {
        if (ready.load(std::memory_order_acquire))
            return status
                + "  Q " + juce::String(static_cast<int>(queuedEventCount.load(std::memory_order_relaxed)))
                + "  S " + juce::String(static_cast<int>(sentEventCount.load(std::memory_order_relaxed)))
                + "  RF " + juce::String(static_cast<int>(renderFailures.load(std::memory_order_relaxed)));
        return lastError.isNotEmpty() ? "EMBEDDED SC OFF" : status;
    }

    bool isReady() const noexcept { return ready.load(std::memory_order_acquire); }

    juce::String getLastError() const
    {
        const juce::ScopedLock lock(engineLock);
        return lastError;
    }

private:
    static void* openLibrary(const juce::String& path)
    {
       #if JUCE_MAC || JUCE_LINUX
        return dlopen(path.toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
       #elif JUCE_WINDOWS
        return LoadLibraryA(path.toRawUTF8());
       #else
        juce::ignoreUnused(path);
        return nullptr;
       #endif
    }

    static void closeLibrary(void* handle)
    {
        if (handle == nullptr)
            return;
       #if JUCE_MAC || JUCE_LINUX
        dlclose(handle);
       #elif JUCE_WINDOWS
        FreeLibrary(static_cast<HMODULE>(handle));
       #endif
    }

    template <typename Function>
    static bool loadSymbol(void* handle, Function& function, const char* name)
    {
       #if JUCE_MAC || JUCE_LINUX
        function = reinterpret_cast<Function>(dlsym(handle, name));
       #elif JUCE_WINDOWS
        function = reinterpret_cast<Function>(GetProcAddress(static_cast<HMODULE>(handle), name));
       #else
        juce::ignoreUnused(handle, name);
        function = nullptr;
       #endif
        return function != nullptr;
    }

    bool loadServerApi()
    {
        for (const auto& candidate : libraryCandidates("GRIDCOLLIDER_SC_LIBRARY",
                                                       "build-supercollider-host/server/scsynth/libscsynth.dylib",
                                                       "libscsynth.dylib"))
        {
            serverLibrary = openLibrary(candidate);
            if (serverLibrary == nullptr)
                continue;

            if (loadSymbol(serverLibrary, worldNew, "World_New")
                && loadSymbol(serverLibrary, worldCleanup, "World_Cleanup")
                && loadSymbol(serverLibrary, worldSendPacket, "World_SendPacket")
                && loadSymbol(serverLibrary, worldRenderHostAudio, "World_RenderHostAudio"))
            {
                return true;
            }

            closeLibrary(serverLibrary);
            serverLibrary = nullptr;
        }

        lastError = "Embedded SuperCollider host-audio library not found";
        status = "EMBEDDED SC MISSING";
        debugLog(lastError);
        return false;
    }

    bool loadLanguageApi()
    {
        for (const auto& candidate : libraryCandidates("GRIDCOLLIDER_SC_LANG_LIBRARY",
                                                       "build-supercollider-host/lang/libweldsclang.dylib",
                                                       "libweldsclang.dylib"))
        {
            languageLibrary = openLibrary(candidate);
            if (languageLibrary == nullptr)
                continue;

            if (loadSymbol(languageLibrary, initialiseLanguage, "WeldSCLang_Initialise")
                && loadSymbol(languageLibrary, compileSynthDef, "WeldSCLang_CompileSynthDef"))
            {
                std::array<char, 4096> error {};
                const auto root = juce::File(GRIDCOLLIDER_ALCHEMY_SC_ROOT).getChildFile("third_party").getChildFile("supercollider");
                if (initialiseLanguage(root.getFullPathName().toRawUTF8(), error.data(), static_cast<int>(error.size())))
                {
                    debugLog("language compiler ready");
                    return true;
                }

                lastError = "Embedded SuperCollider language init failed: " + juce::String(error.data());
                debugLog(lastError);
            }

            closeLibrary(languageLibrary);
            languageLibrary = nullptr;
        }

        if (lastError.isEmpty())
            lastError = "Embedded SuperCollider language compiler not found";

        status = "EMBEDDED SC LANG MISSING";
        debugLog(lastError);
        return false;
    }

    bool loadStarterSynthDefs()
    {
        std::vector<char> masterBytes;
        if (! compileSynth("gcMaster", masterSynthSource(), masterBytes))
            return false;

        debugLog("compiled SynthDef gcMaster (" + juce::String(static_cast<int>(masterBytes.size())) + " bytes)");
        sendPacket(buildOscMessage("/d_recv", { OscArgument::blob(std::move(masterBytes)) }));
        registerSynthName("gcMaster");

        for (const auto& name : { "kick", "snare", "hat", "bass", "tone", "grain", "drone" })
        {
            std::vector<char> bytes;
            if (! compileSynth(name, starterSynthSource(name), bytes))
                return false;

            debugLog("compiled SynthDef " + juce::String(name) + " (" + juce::String(static_cast<int>(bytes.size())) + " bytes)");
            sendPacket(buildOscMessage("/d_recv", { OscArgument::blob(std::move(bytes)) }));
            registerSynthName(name);
        }

        prime();
        return true;
    }

    void createMasterNode()
    {
        sendPacket(buildOscMessage("/s_new",
                                   { OscArgument::string("gcMaster"),
                                     OscArgument::integer(masterNodeId),
                                     OscArgument::integer(1),
                                     OscArgument::integer(masterGroupId),
                                     OscArgument::string("inBus"),
                                     OscArgument::floating(static_cast<float>(masterBus)),
                                     OscArgument::string("out"),
                                     OscArgument::floating(0.0f),
                                     OscArgument::string("master"),
                                     OscArgument::floating(masterLevel),
                                     OscArgument::string("drive"),
                                     OscArgument::floating(masterDrive) }));
        prime();
    }

    bool compileSynth(const juce::String& name, const juce::String& source, std::vector<char>& bytes)
    {
        const auto tempFile = juce::File::createTempFile("gridcollider-sc.scsyndef");
        tempFile.deleteFile();

        std::array<char, 8192> error {};
        if (! compileSynthDef(source.toRawUTF8(),
                              tempFile.getFullPathName().toRawUTF8(),
                              name.toRawUTF8(),
                              error.data(),
                              static_cast<int>(error.size())))
        {
            lastError = "SynthDef compile failed: " + name + " " + juce::String(error.data());
            debugLog(lastError);
            tempFile.deleteFile();
            return false;
        }

        juce::MemoryBlock block;
        if (! tempFile.loadFileAsData(block) || block.getSize() == 0)
        {
            lastError = "SynthDef compiler produced no bytes: " + name;
            debugLog(lastError);
            tempFile.deleteFile();
            return false;
        }

        bytes.assign(static_cast<const char*>(block.getData()), static_cast<const char*>(block.getData()) + block.getSize());
        tempFile.deleteFile();
        return true;
    }

    std::vector<char> packetFor(const NoteEvent& event)
    {
        const auto& fields = event.fields;
        const auto synth = synthForTarget(fields);
        const auto nodeId = nextNodeId++;
        const auto duration = secondsForTicks(fields.durationTicks, bpm.load(std::memory_order_relaxed));
        const auto velocity = juce::jlimit(0.0f, 1.0f, fields.velocity);
        const auto level = levelFor(synth);
        auto pan = panForX(fields.sourceCell.column);

        if (const auto iter = fields.parameters.find("pan"); iter != fields.parameters.end())
            pan = juce::jlimit(-1.0f, 1.0f, iter->second.getFloatValue());

        activeNodeByInstrument[synth] = nodeId;

        return buildOscMessage("/s_new",
                               { OscArgument::string(synth),
                                 OscArgument::integer(nodeId),
                                 OscArgument::integer(1),
                                 OscArgument::integer(sourceGroupId),
                                 OscArgument::string("out"),
                                 OscArgument::floating(static_cast<float>(masterBus)),
                                 OscArgument::string("pitch"),
                                 OscArgument::floating(static_cast<float>(juce::jlimit(0, 127, fields.pitch))),
                                 OscArgument::string("amp"),
                                 OscArgument::floating(velocity * 0.45f * level),
                                 OscArgument::string("sustain"),
                                 OscArgument::floating(duration),
                                 OscArgument::string("pan"),
                                 OscArgument::floating(pan) });
    }

    std::vector<char> packetFor(const TriggerEvent& event)
    {
        const auto trigger = event.triggerName.toLowerCase();
        const auto synth = synthForName(trigger);
        const auto nodeId = nextNodeId++;
        const auto level = levelFor(synth);
        auto pan = panForX(event.fields.sourceCell.column);

        if (const auto iter = event.fields.parameters.find("pan"); iter != event.fields.parameters.end())
            pan = juce::jlimit(-1.0f, 1.0f, iter->second.getFloatValue());

        activeNodeByInstrument[synth] = nodeId;

        return buildOscMessage("/s_new",
                               { OscArgument::string(synth),
                                 OscArgument::integer(nodeId),
                                 OscArgument::integer(1),
                                 OscArgument::integer(sourceGroupId),
                                 OscArgument::string("out"),
                                 OscArgument::floating(static_cast<float>(masterBus)),
                                 OscArgument::string("pitch"),
                                 OscArgument::floating(48.0f),
                                 OscArgument::string("amp"),
                                 OscArgument::floating(0.35f * level),
                                 OscArgument::string("sustain"),
                                 OscArgument::floating(0.18f),
                                 OscArgument::string("pan"),
                                 OscArgument::floating(pan) });
    }

    std::vector<std::vector<char>> packetsFor(const ControlEvent& event)
    {
        std::vector<std::vector<char>> packets;
        const auto parameter = normalisedControlParameter(event.parameterName);
        const auto instrument = synthForTarget(event.fields);
        const auto value = juce::jlimit(0.0f, 1.0f, event.value);

        if (instrument == "master" || parameter == "master")
        {
            masterLevel = value;
            packets.push_back(buildOscMessage("/n_set",
                                              { OscArgument::integer(masterNodeId),
                                                OscArgument::string("master"),
                                                OscArgument::floating(masterLevel) }));
            return packets;
        }

        if (parameter == "drive")
        {
            masterDrive = 0.65f + value * 1.35f;
            packets.push_back(buildOscMessage("/n_set",
                                              { OscArgument::integer(masterNodeId),
                                                OscArgument::string("drive"),
                                                OscArgument::floating(masterDrive) }));
            return packets;
        }

        if (parameter == "level")
            instrumentLevels[instrument] = value;

        if (const auto node = activeNodeFor(instrument); node > 0)
        {
            if (parameter == "level")
            {
                packets.push_back(buildOscMessage("/n_set",
                                                  { OscArgument::integer(node),
                                                    OscArgument::string("amp"),
                                                    OscArgument::floating(value * 0.45f) }));
            }
            else if (parameter == "pan")
            {
                packets.push_back(buildOscMessage("/n_set",
                                                  { OscArgument::integer(node),
                                                    OscArgument::string("pan"),
                                                    OscArgument::floating(value * 2.0f - 1.0f) }));
            }
        }

        return packets;
    }

    void registerSynthName(const juce::String& name)
    {
        const auto trimmed = name.trim();

        if (trimmed.isNotEmpty())
            loadedSynthNames[trimmed.toLowerCase()] = trimmed;
    }

    juce::String synthForName(const juce::String& name) const
    {
        const auto target = name.trim().toLowerCase();

        if (const auto iter = loadedSynthNames.find(target); iter != loadedSynthNames.end())
            return iter->second;

        return instrumentFor(target);
    }

    juce::String synthForTarget(const EventFields& fields) const
    {
        const auto target = fields.targetAddress.value_or(fields.instrumentName).trim().toLowerCase();

        if (target == "master" || target == "/master" || target == "gcmaster")
            return "master";

        if (const auto iter = loadedSynthNames.find(target); iter != loadedSynthNames.end())
            return iter->second;

        const auto instrument = fields.instrumentName.trim().toLowerCase();
        if (const auto iter = loadedSynthNames.find(instrument); iter != loadedSynthNames.end())
            return iter->second;

        if (target.endsWith("/0"))
            return synthForName("tone");
        if (target.endsWith("/1"))
            return synthForName("bass");
        if (target.endsWith("/2"))
            return synthForName("drone");
        if (target.endsWith("/3"))
            return synthForName("grain");
        if (target.endsWith("/9"))
            return synthForName("kick");
        if (target.endsWith("/10"))
            return synthForName("snare");
        if (target.endsWith("/11"))
            return synthForName("hat");

        return synthForName(fields.instrumentName);
    }

    float levelFor(const juce::String& instrument)
    {
        if (const auto iter = instrumentLevels.find(instrument); iter != instrumentLevels.end())
            return iter->second;

        const auto level = defaultLevelFor(instrument);
        instrumentLevels[instrument] = level;
        return level;
    }

    std::int32_t activeNodeFor(const juce::String& instrument) const
    {
        if (const auto iter = activeNodeByInstrument.find(instrument); iter != activeNodeByInstrument.end())
            return iter->second;

        return 0;
    }

    void flushQueuedEvents()
    {
        queuedScratch.clear();
        {
            const juce::ScopedLock lock(queueLock);
            queuedScratch.swap(pendingPackets);
        }

        for (auto& packet : queuedScratch)
        {
            sendPacket(packet);
            sentEventCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void sendPacket(std::vector<char> packet)
    {
        if (world != nullptr && worldSendPacket != nullptr && ! packet.empty())
            [[maybe_unused]] const auto sent = worldSendPacket(world, static_cast<int>(packet.size()), packet.data(), nullptr);
    }

    void prime()
    {
        std::vector<float> scratch(static_cast<std::size_t>(hostRenderQuantum * numOutputChannels), 0.0f);
        for (int i = 0; i < 8; ++i)
            [[maybe_unused]] const auto rendered = worldRenderHostAudio(world, nullptr, scratch.data(), hostRenderQuantum, 0, numOutputChannels);
    }

    static juce::String findPluginPath()
    {
        if (const auto* env = std::getenv("GRIDCOLLIDER_SC_PLUGIN_PATH"))
            return env;

        const auto alchemyPlugins = juce::File(GRIDCOLLIDER_ALCHEMY_SC_ROOT)
                                        .getChildFile("build-supercollider-host")
                                        .getChildFile("server")
                                        .getChildFile("plugins");
        return alchemyPlugins.isDirectory() ? alchemyPlugins.getFullPathName() : juce::String {};
    }

    void releaseUnlocked() noexcept
    {
        ready.store(false, std::memory_order_release);

        if (world != nullptr && worldCleanup != nullptr)
            worldCleanup(world, false);

        world = nullptr;
        closeLibrary(serverLibrary);
        serverLibrary = nullptr;
        // The embedded sclang runtime is process-global. Like Alchemy, keep the
        // language dylib resident once loaded rather than unloading it mid-app.
        languageLibrary = nullptr;
        worldNew = nullptr;
        worldCleanup = nullptr;
        worldSendPacket = nullptr;
        worldRenderHostAudio = nullptr;
        compileSynthDef = nullptr;
        initialiseLanguage = nullptr;
        pendingPackets.clear();
        queuedScratch.clear();
        interleavedOutput.clear();
        currentSampleRate = 0.0;
        maxBlockSize = 0;
        numOutputChannels = 0;
        nextNodeId = 10000;
        activeNodeByInstrument.clear();
        instrumentLevels.clear();
        loadedSynthNames.clear();
        masterLevel = 0.9f;
        masterDrive = 0.88f;
    }

    mutable juce::CriticalSection engineLock;
    mutable juce::CriticalSection synthCompileLock;
    juce::CriticalSection queueLock;
    juce::String lastError;
    juce::String status = "EMBEDDED SC OFF";
    juce::String pluginPath;
    std::vector<float> interleavedOutput;
    std::vector<std::vector<char>> pendingPackets;
    std::vector<std::vector<char>> queuedScratch;
    std::map<juce::String, std::int32_t> activeNodeByInstrument;
    std::map<juce::String, float> instrumentLevels;
    std::map<juce::String, juce::String> loadedSynthNames;
    std::atomic<bool> ready { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<std::uint64_t> tick { 0 };
    std::atomic<bool> playing { false };
    std::atomic<int> renderFailures { 0 };
    std::atomic<int> queuedEventCount { 0 };
    std::atomic<int> sentEventCount { 0 };
    std::int32_t nextNodeId = 10000;
    float masterLevel = 0.9f;
    float masterDrive = 0.88f;
    double currentSampleRate = 0.0;
    int maxBlockSize = 0;
    int numOutputChannels = 0;
    WorldOptions worldOptions;
    World* world = nullptr;
    void* serverLibrary = nullptr;
    void* languageLibrary = nullptr;
    WorldNewFn worldNew = nullptr;
    WorldCleanupFn worldCleanup = nullptr;
    WorldSendPacketFn worldSendPacket = nullptr;
    WorldRenderHostAudioFn worldRenderHostAudio = nullptr;
    CompileSynthDefFn compileSynthDef = nullptr;
    InitialiseLangFn initialiseLanguage = nullptr;
#else
    bool prepare(double, int, int)
    {
        lastError = "GridCollider was built without SuperCollider host-audio headers";
        return false;
    }
    void release() noexcept {}
    void render(juce::AudioBuffer<float>& output) { output.clear(); }
    void enqueue(const std::vector<InternalEvent>&) {}
    void setTransport(double, std::uint64_t, bool) {}
    void setMasterLevel(float) {}
    bool loadSynthDef(const juce::String&, const juce::String&) { return false; }
    bool isReady() const noexcept { return false; }
    juce::String getStatusText() const { return "EMBEDDED SC UNBUILT"; }
    juce::String getLastError() const { return lastError; }
    juce::String lastError;
#endif
};

EmbeddedScAudioEngine::EmbeddedScAudioEngine()
    : impl(std::make_unique<Impl>())
{
}

EmbeddedScAudioEngine::~EmbeddedScAudioEngine() = default;

bool EmbeddedScAudioEngine::prepare(const double sampleRate, const int maximumBlockSize, const int outputChannels)
{
    return impl->prepare(sampleRate, maximumBlockSize, outputChannels);
}

void EmbeddedScAudioEngine::release() noexcept
{
    impl->release();
}

void EmbeddedScAudioEngine::render(juce::AudioBuffer<float>& output)
{
    impl->render(output);
}

void EmbeddedScAudioEngine::enqueue(const std::vector<InternalEvent>& events)
{
    impl->enqueue(events);
}

void EmbeddedScAudioEngine::setTransport(const double bpm, const std::uint64_t tick, const bool playing)
{
    impl->setTransport(bpm, tick, playing);
}

void EmbeddedScAudioEngine::setMasterLevel(const float level)
{
    impl->setMasterLevel(level);
}

bool EmbeddedScAudioEngine::loadSynthDef(const juce::String& name, const juce::String& source)
{
    return impl->loadSynthDef(name, source);
}

bool EmbeddedScAudioEngine::isReady() const noexcept
{
    return impl->isReady();
}

juce::String EmbeddedScAudioEngine::getStatusText() const
{
    return impl->getStatusText();
}

juce::String EmbeddedScAudioEngine::getLastError() const
{
    return impl->getLastError();
}
}
