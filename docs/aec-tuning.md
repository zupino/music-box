# Acoustic echo cancellation tuning

The AEC uses the digital synth/loop mix as a reference and estimates how that signal travels through the nearby speakers and enclosure into both WM8960 microphones. It subtracts the estimate before recording a new loop. It does not remove unrelated room noise, mechanical vibration or nonlinear speaker distortion.

## First test

1. Play a repeating FluidSynth sound or existing loop for 10–20 seconds.
2. Keep the room otherwise quiet while the filter adapts.
3. Record a new loop.
4. Compare with `aec_enabled=false`.

Do not judge only the first second after startup; coefficients begin at zero.

## Delay

`aec_delay_frames` is the most important setting. The default 512 frames is about 10.7 ms at 48 kHz and matches a common JACK setup of two 256-frame periods. Try values around it in 32/64-frame steps: 384, 448, 512, 576, 640. Restart only `music-box.service` after each change.

## Filter length

`aec_taps` covers reflections after the configured delay. At 48 kHz, 128 taps is 2.67 ms and 256 taps is 5.33 ms. Increase it only when the main echo is reduced but a short tail remains. Four adaptive paths make CPU cost important on a Pi 3.

## Adaptation step

`aec_step_size` controls learning speed. Lower is slower/stabler; higher adapts faster but can sound metallic. A practical range is 0.02–0.10.

## Double-talk ratio

Adaptation freezes when microphone energy is too large relative to the speaker reference. Lower values are more conservative; higher values adapt more often, including during voice. If the performer becomes hollow, lower `aec_double_talk_ratio` or `aec_step_size`.

## Safe fallback

```ini
aec_enabled=false
```

The recorder then receives unprocessed stereo microphones without changing the JACK graph.

Physical improvements still help: aim speakers away from microphones, reduce speaker gain while recording, isolate enclosure vibration, use headphones for critical takes, and increase separation where possible.
