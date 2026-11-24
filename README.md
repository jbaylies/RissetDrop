# RissetDrop

An audio effect that makes the beat seem to speed up (or slow down) forever,
the rhythmic version of the Shepard tone. Press **DROP** and the last bar of
whatever is playing keeps accelerating without ever getting faster; release it
and it eases back and lands in time with the track.

Built with [YUP](https://github.com/kunitoki/yup) as VST3, AU, CLAP and a
standalone app, for macOS and Windows.

## How it sounds and how to use it

1. Put RissetDrop on a drum bus, a loop or the whole mix.
2. Press **DROP** (or automate the *Drop* parameter) a bar or two before the
   moment you want to build into. RissetDrop grabs the last *Loop* of audio,
   lined up with your DAW's bar grid, and starts accelerating it over *Ramp*.
3. Release **DROP** on the downbeat, or a little earlier. With *Return: On
   grid*, it picks a trajectory that brings the loop back to normal speed
   exactly in phase with the track, then crossfades back to the live input.

Nothing jumps along the way: the speed never changes abruptly, there is no
pitch step on engage or release, and the input passes through untouched (zero
latency) when it isn't dropping.

## Controls

| Control | What it does |
|---|---|
| **DROP** | Starts and stops the effect. Automatable. |
| **Rate** | How fast it accelerates, in octaves of tempo per bar. Negative slows down; 0 holds a steady blend. Finer near zero. |
| **Ramp** | How long the acceleration takes to fade in, and the shortest time the return takes, in beats. |
| **Loop** | How much audio is looped: 1/4 beat to 4 bars. At very slow tempos (below about 45 BPM at 48 kHz) 4 bars won't fit and it uses 2. |
| **Capture** | *Freeze* loops what was playing when you pressed DROP. *Live* keeps recording: each time a full new loop has played, it replaces the old one (see below). |
| **Focus** | How many voices you hear at once: low is a wide wash of tempos, high is a tight, clearer pulse. |
| **Voices** | 3 to 8 copies of the loop, an octave of tempo apart. Takes effect at the next drop. |
| **Mix** | Dry/wet while dropping. |
| **Tempo source** | *Host* follows the DAW's tempo and bar position; *Manual* uses the Tempo knob. |
| **Tempo** | The tempo to use when there is no host tempo (or in *Manual*). |
| **Return** | *On grid* lands the return in time with the track; *Free* returns over exactly *Ramp*. |
| **Mode** | *Varispeed* lets pitch follow speed, like tape. *Pitch lock* keeps the pitch and changes only the tempo (granular). |

Click a knob's value to type one in: Enter sets it, Escape cancels. Loop
takes things like `2 bars`, `1/2` or `8` (beats). Double-click a knob for its
default.

### Live capture

With *Capture: Live*, the voices keep playing the newest audio instead of a
frozen snapshot, so chord changes and fills come through into the drop. It
never reads where it is writing: the loop is always the last *complete* loop
period, and when the next one has finished recording, the voices crossfade
to it over 50 ms (less for loops under 200 ms). The takes are exactly one
loop apart on the bar grid, so every voice stays where it is in the rhythm;
only the audio under it changes, once per loop. On repetitive material the
swap is inaudible; on changing material it sounds like the loop updating on
the downbeat. The ring around the spiral lights up in Live mode and flashes
as each new take comes in.

You can switch Capture during a drop. Each take is up to one loop period
behind the input (fast voices play it quickly, so they run out of new audio
and wrap within the take until the next one arrives).

The spiral shows the voices: slow ones near the middle, fast ones outside, one
turn per octave. They sit on one ray, which turns as they climb. The glow is
how loud each part of the ladder is, the hand on each dot is where that voice
is reading in the loop, and the dot on the outer ring is your track's position
in the loop.

## Building

You need CMake 3.31 or later, plus Xcode's command line tools on macOS or
Visual Studio 2022 (or its Build Tools) with the C++ workload on Windows.

```bash
git clone --recurse-submodules https://github.com/jbaylies/RissetDrop.git
```

```bash
cd RissetDrop && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

On Windows, configure with `-G "Visual Studio 17 2022" -A x64` and build with
`cmake --build build --config Release --parallel`. The first configure
downloads YUP's dependencies. Builds install the VST3 and CLAP plugins into
your per-user plugin folders; pass `-DRISSETDROP_INSTALL_PLUGINS=OFF` to skip
that.

## Testing

`risset_render_test` runs the DSP engine offline through 28 scenarios
(rates, voice counts, sample rates, odd tempos, quick taps, re-engaging during
a return, pitch lock, live capture, 4-bar loops, two minutes of random
parameter abuse) and checks for
clicks, mismatches at engage and landing, grid alignment, jumps in the
acceleration, allocations in the audio callback, and CPU spikes:

```bash
build/risset_render_test
```

Give it a folder to also write WAV renders of a drum loop to listen to:

```bash
build/risset_render_test renders
```

[DESIGN.md](DESIGN.md) explains the DSP.

### Validation status (main, after v0.1.0)

| check | macOS (arm64) | Windows (x64, MSVC) |
|---|---|---|
| `risset_render_test` | 28/28 | 28/28 |
| VST3 SDK `validator` | 47 passed, 0 failed | 47 passed, 0 failed |
| `auval -v aufx RsDp Sous` | AU VALIDATION SUCCEEDED | n/a |
| clap-validator 0.4.1 | not run | 28 passed, 4 failed, 12 skipped (v0.1.0) |

The four clap-validator failures:

- `process-audio-basic-in-place` is a known bug in YUP's CLAP wrapper (its
  in-place port pairing), which every YUP effect currently hits.
- `state-reproducibility-basic`, `-binary` and `-buffered` are still open.
  A hand-written CLAP host round-trips the state correctly on both platforms:
  it sets parameters through `process()` events, saves, reloads into a fresh
  instance (before or after activation), and reads back identical values. So
  the cause is something specific to how the validator drives the plugin,
  possibly YUP's CLAP `process()` dropping a block's events when its try-lock
  fails. Not yet found.

## Credits

The Risset rhythm idea is Jean-Claude Risset's; Dan Stowell's paper
*Scheduling and composing with Risset eternal accelerando rhythms* (ICMC 2011)
describes the mechanics, and Timo Hoogland's
[risset-rhythm-effect](https://github.com/tmhglnd/risset-rhythm-effect) Max
patch was the starting reference. MIT licensed, see [LICENSE](LICENSE).
