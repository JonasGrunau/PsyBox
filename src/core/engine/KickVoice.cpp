#include "KickVoice.h"

namespace psy
{

using namespace dsp;

namespace
{
    constexpr float kLand = 0.04978707f;   // e^-3: a pitch stage lands on its target after 3 time constants
    constexpr float kGain = 0.5f;          // body peak before the master volume, level with the synth patches
    constexpr float kChokeMs = 3.f;
    constexpr float kTickMs = 0.08f;       // tick pulse width; fixed in time so the level ignores Quality

    int shaperFor (int driveType) noexcept
    {
        switch (driveType)
        {
            case 0: return AdaaShaper::Soft;
            case 2: return AdaaShaper::Hard;
            default: return AdaaShaper::Tube;
        }
    }

    // A pitch stage's decay normalised so it reaches exactly 0 at 3 time constants.
    float landed (float g) noexcept { return std::max (0.f, (g - kLand) * (1.f / (1.f - kLand))); }
} // namespace

KickSettings KickSettings::from (const ParamState& p) noexcept
{
    KickSettings s;
    s.tuneHz = p.get (P::KickTune);
    s.keyTrack = p.on (P::KickKeyTrack);
    s.startHz = p.get (P::KickStart);
    s.snapMs = p.get (P::KickSnap);
    s.kneeHz = p.get (P::KickKnee);
    s.sweepMs = p.get (P::KickSweep);
    s.attackMs = p.get (P::KickAttack);
    s.holdMs = p.get (P::KickHold);
    s.decayMs = p.get (P::KickDecay);
    s.punch = p.get (P::KickPunch);
    s.punchMs = p.get (P::KickPunchLen);
    s.clickType = p.geti (P::KickClickType);
    s.clickLevel = p.get (P::KickClickLevel);
    s.clickMs = p.get (P::KickClickDecay);
    s.clickHz = p.get (P::KickClickTone);
    s.shaper = shaperFor (p.geti (P::KickDriveType));
    s.driveDb = p.get (P::KickDrive);
    s.toneHz = p.get (P::KickTone);
    s.velSens = p.get (P::VelSens);
    return s;
}

float kickBodyHz (const KickSettings& s, int note) noexcept
{
    if (! s.keyTrack)
        return s.tuneHz;
    const float centre = 69.f + 12.f * std::log2 (s.tuneHz / 440.f);
    // the note's pitch class, moved by octaves into [centre - 6, centre + 6)
    const float shift = std::floor ((centre + 6.f - (float) note) / 12.f) * 12.f;
    return noteToHz ((float) note + shift);
}

float kickPitchAt (const KickSettings& s, int note, float t) noexcept
{
    const float body = kickBodyHz (s, note);
    const float r = body / s.tuneHz;
    const float start = s.startHz * r;
    const float knee = std::clamp (s.kneeHz * r, body, std::max (start, body));
    const float e1 = landed (std::exp (-t * 1000.f / s.snapMs));
    const float e2 = landed (std::exp (-t * 1000.f / s.sweepMs));
    return body + (knee - body) * e2 + (std::max (start, knee) - knee) * e1;
}

void KickVoice::prepare (float base, float os)
{
    setRates (base, os);
}

void KickVoice::setRates (float base, float os)
{
    baseRate = base;
    osRate = os;
    osFactor = std::max (1, (int) std::lround (os / base));
    amp.setSampleRate (osRate);
    dc.prepare (osRate);
    kill();
}

void KickVoice::kill() noexcept
{
    active = false;
    choking = false;
    amp.kill();
}

void KickVoice::trigger (int n, float vel, const VoiceContext& ctx) noexcept
{
    const auto s = KickSettings::from (*ctx.p);
    note = n;
    velocity = vel;
    active = true;
    choking = false;
    released = false;
    chokeGain = 1.f;
    elapsed = 0;
    phase = 0.0;
    blipPhase = 0.0;
    g1 = g2 = 1.f;
    punchEnv = 1.f;
    clickEnv = 1.f;
    shaper.reset();
    dc.reset();
    clickBp.reset();
    tone.reset();
    noise.reset (0x6B69636Bu);   // the same noise every hit
    amp.kill();
    amp.set (s.attackMs, 1.f, 1.f, s.decayMs);
    amp.noteOn();
}

void KickVoice::choke() noexcept
{
    if (! active || choking) return;
    choking = true;
    chokeStep = 1.f / std::max (1.f, kChokeMs * 0.001f * osRate);
}

void KickVoice::raiseVelocity (float v) noexcept
{
    velocity = std::max (velocity, v);
}

void KickVoice::render (float* outL, float* outR, int chunkOffsetBase, int nBase, const VoiceContext& ctx) noexcept
{
    if (! active) return;
    const auto s = KickSettings::from (*ctx.p);
    renderMono (outL, outR, chunkOffsetBase * osFactor, nBase * osFactor, s);
}

void KickVoice::renderMono (float* outL, float* outR, int offsetOs, int numOs, const KickSettings& s) noexcept
{
    // pitch: body, knee and start all follow the key-track ratio
    const float nyq = 0.45f * baseRate;
    const float body = std::min (kickBodyHz (s, note), nyq);
    const float r = body / s.tuneHz;
    const float start = std::min (s.startHz * r, nyq);
    const float knee = std::clamp (s.kneeHz * r, body, std::max (start, body));
    const float d1 = std::exp (-1.f / (s.snapMs * 0.001f * osRate));
    const float d2 = std::exp (-1.f / (s.sweepMs * 0.001f * osRate));

    amp.set (s.attackMs, 1.f, 1.f, s.decayMs);
    const int64_t holdEnd = (int64_t) ((s.attackMs + s.holdMs) * 0.001f * osRate);
    const float dPunch = std::exp (-1.f / (s.punchMs * 0.001f * osRate));
    const float dClick = std::exp (-6.9f / (s.clickMs * 0.001f * osRate));   // -60 dB at clickMs

    const float g = dbToGain (s.driveDb);
    const float makeup = 1.f / (float) std::max ({ std::abs (AdaaShaper::f (g, s.shaper)), std::abs (AdaaShaper::f (-g, s.shaper)), 1e-6 });
    const float velGain = 1.f - s.velSens * (1.f - velocity);

    clickBp.setG (prewarp (s.clickHz, osRate), 1.f);
    tone.setG (prewarp (s.toneHz, osRate), 1.41421356f);
    const int tickLen = std::max (1, (int) std::lround (kTickMs * 0.001f * osRate));
    // band-passed white noise loses level as the rate rises and gains it as the band widens
    const float noiseGain = std::sqrt (osRate / std::max (s.clickHz, 300.f));
    const float blipInc = std::min (s.clickHz, 0.22f * osRate) / osRate;   // x2 at the start stays below Nyquist

    for (int i = 0; i < numOs; ++i)
    {
        if (! released && elapsed >= holdEnd)
        {
            amp.noteOff();
            released = true;
        }
        const float a = amp.process();
        if (! amp.active() && ! choking)
            choke();   // the saturator's DC blocker still holds a little offset: fade it rather than drop it

        g1 *= d1;
        g2 *= d2;
        const float e1 = landed (g1), e2 = landed (g2);
        const float hz = body + (knee - body) * e2 + (std::max (start, knee) - knee) * e1;
        phase += (double) (hz / osRate);
        if (phase >= 1.0) phase -= 1.0;

        // bounded punch: full level at the hit, the body fades in underneath
        const float env = a + s.punch * (1.f - std::min (a, 1.f)) * punchEnv;
        punchEnv *= dPunch;

        const float x = std::sin ((float) (kTwoPi * phase)) * env;
        float y = (float) shaper.process (x * g, s.shaper) * makeup;
        y = dc.process (y);

        // click layer
        float c = 0.f;
        if (s.clickLevel > 0.f && clickEnv > 1e-4f)
        {
            switch (s.clickType)
            {
                case 1:   // tick: a short pulse ringing through the band-pass
                    c = clickBp.process (elapsed < tickLen ? 1.f : 0.f).bp * 2.f;
                    break;
                case 2:   // blip: a sine that drops an octave as it fades
                    blipPhase += (double) (blipInc * (1.f + clickEnv));
                    if (blipPhase >= 1.0) blipPhase -= 1.0;
                    c = std::sin ((float) (kTwoPi * blipPhase));
                    break;
                default:  // band-passed noise
                    c = clickBp.process (noise.process (0, 0.0)).bp * noiseGain;
                    break;
            }
            c *= clickEnv * s.clickLevel;
        }
        clickEnv *= dClick;

        y = tone.process (y + c).lp;
        y *= kGain * velGain;

        if (choking)
        {
            chokeGain -= chokeStep;
            if (chokeGain <= 0.f)
            {
                chokeGain = 0.f;
                active = false;
                amp.kill();
                break;
            }
            y *= chokeGain;
        }

        outL[offsetOs + i] += y;
        if (outR != nullptr) outR[offsetOs + i] += y;
        lastEnv = env;
        lastHz = hz;
        ++elapsed;
    }
}

void KickVoice::renderOffline (const ParamState& p, int note, float velocity, float sampleRate, int numSamples,
                               float* out, float* env, float* hz)
{
    KickVoice k;
    k.prepare (sampleRate, sampleRate);
    VoiceContext ctx;
    ctx.p = &p;
    ctx.baseRate = ctx.osRate = sampleRate;
    ctx.osFactor = 1;
    k.trigger (note, velocity, ctx);
    const auto s = KickSettings::from (p);
    float y = 0.f;
    for (int i = 0; i < numSamples; ++i)
    {
        y = 0.f;
        if (k.active)
            k.renderMono (&y, nullptr, 0, 1, s);
        if (out != nullptr) out[i] = y;
        if (env != nullptr) env[i] = k.active ? k.lastEnv : 0.f;
        if (hz != nullptr) hz[i] = k.lastHz;
    }
}

} // namespace psy
