# License layout

- `../LICENSE` applies to project-owned C source, tests, shell scripts, systemd
  templates and documentation.
- Each project-owned source/script carries `SPDX-License-Identifier: MIT`.
- No third-party source files are currently vendored, so their full license
  texts are not duplicated in this directory.
- External packages and data are catalogued in `../THIRD_PARTY.md` and retain
  their upstream terms.

If third-party files are ever added, place their unmodified license text here,
add SPDX identifiers to the files where possible, and update
`THIRD_PARTY.md` with provenance and version information.
