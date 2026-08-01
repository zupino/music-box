# Contributing

Keep the project understandable on a Raspberry Pi 3:

- prefer small C11 modules over frameworks;
- do not allocate, log, sleep, lock or perform I/O in the JACK callback;
- keep third-party source out of the repository unless there is a strong, documented reason and its license is preserved;
- add hardware-independent tests for transport, state-machine or DSP changes;
- run `make test` and a real JACK build before submitting changes;
- use `// SPDX-License-Identifier: MIT` or the matching shell/YAML comment.

AEC changes should include a synthetic convergence test and report CPU impact on at least a Raspberry Pi 3. Quantization changes should test exact frame boundaries at both integer and fractional BPM values.
