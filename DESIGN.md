# RissetDrop: how the DSP works

All of it lives in `source/dsp`, plain C++ with no YUP dependency, so the
offline harness in `tools/render_test` runs exactly the code the plugin does.

## 1. Why a loop

Speeding up live input with no lookahead can't be done: a read head moving
faster than real time would have to read audio that hasn't arrived yet. So
pressing DROP **captures a loop**: the last `Loop` beats from a history buffer
that always records. The Risset voices play that loop. Latency is zero, and
the dry signal passes through bit-exact while idle.

With a host transport, the loop is aligned to the bar grid. If the transport
is at loop position `p0` (from `ppq`), the loop starts at history time
`A = w0 − L − p0` (`w0` = now, `L` = loop length), so loop position `p0` plays
what was heard exactly one loop ago. On repetitive material that is
continuous with the live input, and on a tone whose period divides the loop
it is sample-identical (the harness checks this to −125 dB).

Recording continues during a drop, so the newest few milliseconds of the loop
(still inside the mipmap filters at engage) arrive before any voice reads
them. It stops only when the next write would overwrite the loop, and resumes
with a short splice when the drop ends.

The history holds two loops of up to 21 s (2^21 samples at 44.1 or 48 kHz):
the loop can start up to two loops back, since `p0` can be nearly `L`. A loop
that doesn't fit is halved until it does, so it still holds whole beats.

**Live capture.** Recording into the loop the voices read would put a write
head in their path: any voice not at exactly 1× crosses it, every
`L / |s − 1|` seconds, and each crossing jumps a whole loop in time. Live
mode never does that. The voices read only *completed* loop periods
("takes"). Once the next whole period has been recorded and has cleared the
mipmap filters (`A + 2L ≤ latest safe time`), the loop start moves on by
`L`:

```
A ← A + kL     (k ≥ 1, the newest complete take)
```

and for 50 ms (at most `L/4`) every read is a raised-cosine crossfade from
the old take to the new one at the same loop position `P`. Since takes are
whole loops apart on the bar grid, no voice moves and the rhythm is
untouched; only the audio under each voice changes. Each voice still wraps
within its take, with the usual loop-point crossfade. Recording continues,
guarded so the write head stays clear of the oldest audio any read can reach
(the old take, during a crossfade).

If recording paused (a long frozen drop) and Live is switched on later, the
first swap goes to the newest take before the pause; recording then resumes,
and later takes are placed by grid time, not history time, since history
time stood still during the pause (the engine counts the samples it didn't
record). No take straddles the pause. The harness checks this on a tone
that is periodic in the loop: if the takes slipped off the grid, the landing
would not match the input.

## 2. The history buffer (`HistoryBuffer`)

A stereo ring buffer of about 22 s at 48 kHz, plus a mipmap: level *n* is
level *n−1* run through a 31-tap Kaiser half-band low-pass (β = 8) and
decimated by 2, up to level 4 (16×). Each level records which history time its
sample 0 stands for, filter delay included, so reads line up across levels to
the sample. A voice at speed `s` reads level `log2(s) + 0.25`, blending the
two nearest levels (trilinear), with 4-point Catmull-Rom interpolation within
a level. Fast voices therefore don't alias.

## 3. The ladder

`V` voices (3–8), one octave of speed apart. With `C = V/2`:

```
speed_j = 2^(j + φ − C),   j = 0 … V−1,   φ ∈ [0, 1)
```

`φ` is the shared log-speed phasor and `φ'` (octaves per sample) is the
acceleration. When `φ` passes 1, every voice moves up a slot and the top one
(silent there) wraps to the bottom; passing 0 does the reverse.

**Positions.** For the illusion, every voice has to stay rhythmically locked:
a voice at twice the speed must hit every beat the slower one hits. All read
positions therefore come from one accumulator `M`:

```
P_j = 2^(j + F) · M  mod L,        dM/dt = 2^(φ − C − F)
```

With `F = 16`, `M` can be reduced mod `L` at any time, and halved when `φ`
wraps up, without moving any voice (for all labels `j ≥ 1 − F`), because the
ambiguity is always a whole number of loops. `M` is integrated with the
trapezoidal rule, so a voice's position is exact to well under a sample over
any sweep.

**Window.** Voice `j` sits at `x = (j + φ)/V` on the ladder. Its gain is

```
w(x) = sin²(πx) · exp(−k (x − ½)²)
```

normalised by `(Σw²)^0.375 (Σw)^0.25`: mostly power normalisation, since
voices share downbeats and are partly correlated. A lone voice gets exactly 1.
*Focus* sets `k` for the steady blend (σ from 0.9 to 0.11). During transitions
`k` is interpolated (in log) toward a "lock" width of σ = 0.02, where only the
voice at the centre, at exactly 1×, can be heard (neighbours are below
−120 dB).

## 4. Transitions

The phases are `idle → engaging → engaged → releasing → landing → idle`.
Pressing DROP during a release or landing goes back to engaging from wherever
it has got to.

**Engaging.** The loop is captured with `φ` on an aligned phase (a voice at
1×, in the window's centre, reading the loop in phase with the transport). The
dry/wet crossfade takes 15 ms between two equal signals. Over `Ramp`, with
`s = smootherstep(u)`:

```
φ'     = v0 + (rate − v0) · s
window = e0 + (1 − e0) · s
```

Smootherstep has zero slope and curvature at both ends, so the speed of every
voice is C² from rest into the acceleration.

**Engaged.** `φ' = rate`, the rate parameter smoothed by two cascaded 40 ms
one-poles, so automation (including through zero) stays smooth.

**Releasing.** `φ` follows a quintic Hermite from `(φr, φ'r, 0)` to
`(φr + Δ, 0, 0)` over `T` samples:

```
φ(u) = φr + m0 · (u − 6u³ + 8u⁴ − 3u⁵) + Δ · (10u³ − 15u⁴ + 6u⁵),   m0 = φ'r · T
```

Here `φr + Δ` is an aligned phase, so the landing voice ends at exactly 1×.
`φ'(u) = (1−u)² [m0 (1 + 2u − 15u²) + 30 Δ u²] / T`, so the path is monotonic
whenever `Δ` has the sign of `m0` and `|Δ| ≥ |m0|/2`. Meanwhile the window
narrows back to the lone 1× voice.

**Landing on the grid.** The voice that ends at 1× has label `j = C − φr − Δ`
and lands at

```
P_end(T) = 2^(j+F) · M_r + T · ∫₀¹ 2^(φ(u) − φr − Δ) du      (mod L)
```

The transport will be at `D_r + T`. The planner looks for `T` and `Δ` that
make these equal:

- Candidates `Δ` are a few aligned phases just behind (for short taps) and,
  from the nearest monotonic target, five octaves on in the direction of
  travel. Further targets keep rising (or falling) longer while they ease off,
  which sweeps the error faster.
- Each candidate's `T` runs from its shortest allowed return to three loops
  (or 3 s) beyond `Ramp`. The shortest allowed return keeps the sweep's peak
  speed under 1.25 × max(current rate, set rate, 1 oct/s), so short ramps
  stretch rather than whoosh.
- Each candidate is scanned (64 steps, a 12-interval Simpson integral from a
  compile-time table) for the first zero crossing of the wrapped error, which
  is then bisected. The winner (shortest, nudged toward smaller sweeps) is
  polished with secant steps on a 256-interval integral. Candidates that
  can't beat the best score are pruned.

The landing error is under 0.001 samples in every harness scenario, and the
worst release-planning block takes under 20% of a 32-sample buffer. If no
alignment is possible (or *Return* is *Free*), the return takes `Ramp` (or
longer, under the speed cap) and simply isn't phase-locked.

**Landing.** 30 ms crossfade from the 1× loop voice back to the live input.
On the grid, those two are the same audio in phase (−124 dB difference on the
test tone).

## 5. Pitch lock

Each voice also has two Hann grains, 40 ms long and half a grain apart, read
at 1× from where the voice is now. The grains therefore follow the voice's
position (its tempo) without its pitch. At 1× the grains read exactly the
voice's own samples, so engage and landing stay seamless. Switching modes
crossfades over 20 ms.

## 6. Real-time safety

- `prepare()` allocates everything. `process()` allocates nothing (the harness
  counts `operator new` calls during it), takes no locks, and makes no system
  calls.
- Telemetry for the UI goes through a lock-free triple buffer, and parameters
  are atomics.
- The processor runs under `yup::ScopedNoDenormals`.
- Reported latency is 0.
