# Validation scope and local commands

Default-branch validation targets master, not a newly invented main. PR/manual runs preserve the complete Linux/macOS/Windows and PM3/PM5 FPGA jobs and existing path rules. Feature push/PR overlap is removed; validation gains stale-run cancellation. CodeQL retains its language matrix and schedule. This workflow-only change does not establish local hardware, native OS or FPGA test execution.

## Local entry points

```sh
make clean
make all
make check
# Use the documented compiler, Make/CMake, RDV4/PM5 and native-OS variants when affected.
# Complete platform/FPGA commands and dependencies remain in their workflows.
```

Feature branches without an open PR use local commands. A local pass does not replace validation of the submitted revision. Main/release publication keeps the workflow's existing authorization and artifact conditions.

CI optimization batch: November 7, 2026. Workflow syntax and focused orchestration/selection checks are separate from complete application, native-platform and research validation.

## Homebrew dependency follow-up

The macOS run exposed an obsolete `openssl@3.6` include/library path in `tools/mfulc_des_brute/Makefile`. The tool now uses the installed `openssl@3` formula, explicitly installed by both Make jobs. The warning policy and platform variants are retained.

Local verification: this standalone tool built warning-free with GCC on Linux. A Clang build was attempted but Clang is not installed in this executor. The preceding build commit message incorrectly described Clang as validated; this note corrects that statement. Local macOS, Windows and full firmware builds were not performed. The fresh macOS workflow is the verification of the Homebrew path.
