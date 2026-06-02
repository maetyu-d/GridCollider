/*
    GridColliderBridge.scd

    Starter SuperCollider bridge for GridCollider.

    In GridCollider, connect OSC to:
        host: 127.0.0.1
        port: NetAddr.langPort, usually 57120

    Incoming OSC schema:
        /gc/note    instrument pitch velocity duration x y tick
        /gc/control target parameter value x y tick
        /gc/trigger name x y tick

    Add your own SynthDefs by:
        1. Defining SynthDef(\yourName, { ... }).add
        2. Adding \yourName to ~gcSynthNames
        3. Mapping incoming instrument or trigger names below
*/

(
s = Server.default;
s.options.numOutputBusChannels = 2;
s.boot;

s.waitForBoot({
    Routine({
        // Shared conventions for all starter SynthDefs:
        // freq is Hz, amp is 0..1, sustain is seconds, pan is -1..1.
        // cutoff, rq, mix, and rate are included where they make musical sense
        // so /gc/control can change them while a synth is alive.

        SynthDef(\kick, { |out = 0, freq = 52, amp = 0.8, sustain = 0.35, pan = 0, cutoff = 120, rq = 0.4|
            var ampEnv = EnvGen.kr(Env.perc(0.001, sustain.clip(0.04, 2.0), amp, -4), doneAction: 2);
            var clickEnv = EnvGen.kr(Env.perc(0.001, 0.035, amp * 0.35));
            var pitchEnv = EnvGen.kr(Env([freq * 2.8, freq], [0.045], \exp));
            var body = SinOsc.ar(pitchEnv) * ampEnv;
            var click = BPF.ar(WhiteNoise.ar, cutoff.clip(80, 6000), rq.clip(0.05, 0.95)) * clickEnv;
            Out.ar(out, Pan2.ar((body + click).tanh, pan));
        }).add;

        SynthDef(\snare, { |out = 0, freq = 190, amp = 0.5, sustain = 0.22, pan = 0, cutoff = 2600, rq = 0.45, mix = 0.65|
            var noiseEnv = EnvGen.kr(Env.perc(0.002, sustain.clip(0.04, 2.0), amp, -4), doneAction: 2);
            var toneEnv = EnvGen.kr(Env.perc(0.001, 0.09, amp * (1 - mix.clip(0, 1))));
            var noise = BPF.ar(WhiteNoise.ar, cutoff.clip(400, 10000), rq.clip(0.05, 0.95)) * noiseEnv * mix.clip(0, 1);
            var tone = SinOsc.ar(freq.clip(80, 600)) * toneEnv;
            Out.ar(out, Pan2.ar((noise + tone).tanh, pan));
        }).add;

        SynthDef(\hat, { |out = 0, freq = 7000, amp = 0.28, sustain = 0.08, pan = 0, cutoff = 6200, rq = 0.25|
            var env = EnvGen.kr(Env.perc(0.001, sustain.clip(0.015, 0.5), amp, -7), doneAction: 2);
            var metal = HPF.ar(WhiteNoise.ar, cutoff.clip(1200, 12000));
            var sig = BPF.ar(metal, freq.clip(2000, 12000), rq.clip(0.05, 0.95)) * env;
            Out.ar(out, Pan2.ar(sig, pan));
        }).add;

        SynthDef(\bass, { |out = 0, freq = 110, amp = 0.35, sustain = 0.35, pan = 0, cutoff = 900, rq = 0.28, mix = 0.5|
            var env = EnvGen.kr(Env.linen(0.006, sustain.clip(0.02, 8.0), 0.08, amp), doneAction: 2);
            var osc = Mix([Saw.ar(freq), Pulse.ar(freq * 0.5, 0.45)]) * 0.45;
            var sub = SinOsc.ar(freq * 0.5) * mix.clip(0, 1);
            var sig = RLPF.ar(osc + sub, cutoff.clip(60, 12000), rq.clip(0.05, 0.95)) * env;
            Out.ar(out, Pan2.ar(sig.tanh, pan));
        }).add;

        SynthDef(\tone, { |out = 0, freq = 440, amp = 0.25, sustain = 0.3, pan = 0, cutoff = 5000, rq = 0.4, mix = 0.25|
            var env = EnvGen.kr(Env.linen(0.005, sustain.clip(0.02, 8.0), 0.08, amp), doneAction: 2);
            var pure = SinOsc.ar(freq);
            var edge = VarSaw.ar(freq * 1.005, 0, 0.45);
            var sig = XFade2.ar(pure, RLPF.ar(edge, cutoff.clip(80, 14000), rq.clip(0.05, 0.95)), mix.clip(0, 1).linlin(0, 1, -1, 1));
            Out.ar(out, Pan2.ar(sig * env, pan));
        }).add;

        SynthDef(\grain, { |out = 0, freq = 440, amp = 0.25, sustain = 0.8, pan = 0, rate = 18, mix = 0.5|
            var env = EnvGen.kr(Env.linen(0.02, sustain.clip(0.05, 12.0), 0.18, amp), doneAction: 2);
            var trig = Impulse.ar(rate.clip(1, 80));
            var grainDur = mix.clip(0, 1).linlin(0, 1, 0.015, 0.16);
            var sig = GrainSin.ar(2, trig, grainDur, freq * LFNoise1.kr(rate.clip(1, 80)).range(0.75, 1.5), pan);
            Out.ar(out, sig * env);
        }).add;

        SynthDef(\drone, { |out = 0, freq = 110, amp = 0.22, sustain = 4.0, pan = 0, cutoff = 1400, rq = 0.3, mix = 0.4|
            var env = EnvGen.kr(Env.linen(0.5, sustain.clip(0.2, 60.0), 1.2, amp), doneAction: 2);
            var source = Mix(VarSaw.ar(freq * [0.5, 1, 1.01], 0, [0.25, 0.45, 0.5])) * 0.35;
            var shimmer = SinOsc.ar(freq * [1.5, 2.01]).sum * 0.15 * mix.clip(0, 1);
            var sig = RLPF.ar(source + shimmer, cutoff.clip(80, 12000), rq.clip(0.05, 0.95));
            Out.ar(out, Pan2.ar(sig.tanh * env, pan));
        }).add;

        s.sync;

        ~gcGroup = Group.head(s);
        ~gcActive = IdentityDictionary.new;

        // Starter synth names. Add custom SynthDef names here.
        ~gcSynthNames = [\kick, \snare, \hat, \bass, \tone, \grain, \drone];

        // Map incoming GridCollider instrument names to SynthDefs.
        // GridCollider's current Orca MIDI operators emit "midi" and "mono".
        ~gcInstrumentMap = IdentityDictionary[
            \midi -> \tone,
            \mono -> \bass,
            \kick -> \kick,
            \snare -> \snare,
            \hat -> \hat,
            \bass -> \bass,
            \tone -> \tone,
            \grain -> \grain,
            \drone -> \drone
        ];

        // Map control targets to active synth slots.
        // Change these if you want a MIDI/OSC-like target to control a specific SynthDef.
        ~gcTargetMap = IdentityDictionary[
            "midi://channel/0".asSymbol -> \tone,
            "midi://channel/1".asSymbol -> \bass,
            "midi://channel/2".asSymbol -> \drone,
            "all".asSymbol -> \all
        ];

        // Map incoming control names to SynthDef control arguments.
        // Unknown names are used directly, so /gc/control bass cutoff 0.7 ...
        // will set \cutoff on the active bass synth.
        ~gcControlMap = IdentityDictionary[
            \cc1 -> \cutoff,
            \cc2 -> \rq,
            \cc7 -> \amp,
            \cc10 -> \pan,
            \cc11 -> \mix,
            \cc74 -> \cutoff,
            \pitchbend -> \freq
        ];

        // Map trigger names to one-shot actions.
        // Add entries here to trigger your own synths or routines.
        ~gcTriggerMap = IdentityDictionary[
            \kick -> { |x, y, tick| Synth(\kick, [\amp, 0.85, \pan, ~gcPanFromX.(x)], ~gcGroup) },
            \snare -> { |x, y, tick| Synth(\snare, [\amp, 0.55, \pan, ~gcPanFromX.(x)], ~gcGroup) },
            \hat -> { |x, y, tick| Synth(\hat, [\amp, 0.32, \pan, ~gcPanFromX.(x)], ~gcGroup) }
        ];

        ~gcTempoBPM = 120;
        ~gcTicksPerBeat = 4;

        ~gcTicksToSeconds = { |ticks|
            (ticks.max(1) * (60 / ~gcTempoBPM / ~gcTicksPerBeat)).max(0.02)
        };

        ~gcPanFromX = { |x|
            x.asFloat.linlin(0, 63, -0.85, 0.85).clip(-1, 1)
        };

        ~gcScaleParam = { |parameter, value|
            var v = value.asFloat.clip(0, 1);

            switch(parameter.asSymbol,
                \cutoff, { v.max(0.001).linexp(0.001, 1, 80, 12000) },
                \freq, { v.max(0.001).linexp(0.001, 1, 35, 2200) },
                \pan, { v.linlin(0, 1, -1, 1) },
                \rq, { v.linlin(0, 1, 0.05, 0.95) },
                \sustain, { v.linlin(0, 1, 0.02, 8.0) },
                \rate, { v.linlin(0, 1, 1, 80) },
                \amp, { v },
                \mix, { v },
                { v }
            )
        };

        ~gcResolveSynth = { |instrument|
            var name = (~gcInstrumentMap[instrument.asSymbol] ? instrument.asSymbol);
            if(~gcSynthNames.includes(name).not) { name = \tone };
            name
        };

        ~gcResolveTarget = { |target|
            var key = target.asString.asSymbol;
            ~gcTargetMap[key] ? key
        };

        ~gcRememberSynth = { |instrument, synthName, synth|
            ~gcActive[instrument.asSymbol] = synth;
            ~gcActive[synthName.asSymbol] = synth;
        };

        ~gcSetActive = { |target, parameter, value|
            var resolvedTarget = ~gcResolveTarget.(target);
            var mappedParameter = (~gcControlMap[parameter.asSymbol] ? parameter.asSymbol);
            var scaledValue = ~gcScaleParam.(mappedParameter, value);

            if(resolvedTarget == \all) {
                ~gcActive.keysValuesDo({ |key, synth|
                    if(synth.notNil) { synth.set(mappedParameter, scaledValue) };
                });
            } {
                var synth = ~gcActive[resolvedTarget];
                if(synth.notNil) {
                    synth.set(mappedParameter, scaledValue);
                } {
                    ("GridColliderBridge: no active synth for target " ++ target).postln;
                };
            };
        };

        OSCdef(\gcNote, { |msg, time, addr, recvPort|
            var instrument = msg[1].asString.asSymbol;
            var pitch = msg[2].asFloat;
            var velocity = msg[3].asFloat.clip(0, 1);
            var durationTicks = msg[4].asFloat;
            var x = msg[5].asInteger;
            var y = msg[6].asInteger;
            var tick = msg[7].asInteger;
            var synthName = ~gcResolveSynth.(instrument);
            var sustain = ~gcTicksToSeconds.(durationTicks);
            var freq = pitch.midicps;
            var synth;

            synth = Synth(synthName, [
                \freq, freq,
                \amp, velocity,
                \sustain, sustain,
                \pan, ~gcPanFromX.(x),
                \cutoff, pitch.linexp(24, 96, 180, 7000).clip(80, 12000)
            ], ~gcGroup);

            ~gcRememberSynth.(instrument, synthName, synth);
            ("GridColliderBridge note: " ++ instrument ++ " -> " ++ synthName ++ " pitch " ++ pitch ++ " tick " ++ tick).postln;
        }, '/gc/note');

        OSCdef(\gcControl, { |msg, time, addr, recvPort|
            var target = msg[1].asString;
            var parameter = msg[2].asString.asSymbol;
            var value = msg[3].asFloat.clip(0, 1);
            var x = msg[4].asInteger;
            var y = msg[5].asInteger;
            var tick = msg[6].asInteger;

            ~gcSetActive.(target, parameter, value);
            ("GridColliderBridge control: " ++ target ++ " " ++ parameter ++ "=" ++ value ++ " tick " ++ tick).postln;
        }, '/gc/control');

        OSCdef(\gcTrigger, { |msg, time, addr, recvPort|
            var name = msg[1].asString.asSymbol;
            var x = msg[2].asInteger;
            var y = msg[3].asInteger;
            var tick = msg[4].asInteger;
            var action = ~gcTriggerMap[name];

            if(action.notNil) {
                action.value(x, y, tick);
            } {
                ("GridColliderBridge trigger: " ++ name ++ " at " ++ x ++ "," ++ y ++ " tick " ++ tick).postln;
            };
        }, '/gc/trigger');

        ("GridColliderBridge ready. Listening on OSC port " ++ NetAddr.langPort).postln;
        "Add SynthDefs above, then update ~gcSynthNames, ~gcInstrumentMap, or ~gcTriggerMap.".postln;
    }).play;
});
)
