Date: 2026-03-21
Problem: ARM64 extended inline asm support crashed when using FP/SIMD constraints and produced incorrect save/restore and shift encodings.
Root Cause: The inline asm layer mixed architectural register numbers with arm64-gen.c's internal allocator register numbers. FP/SIMD operands were allocated as synthetic 32..63 registers even though the backend only exposes internal FP registers `TREG_F(0..7)`. The save/restore helper also encoded STP/LDP/STR/LDR fields incorrectly, and shift aliases were not implemented according to the A64 instruction definitions.
Solution: Keep assembler parsing on architectural register numbers, but allocate inline asm operands and clobbers using the backend's internal FP register range. Implement official AArch64 operand modifiers in `tccasm.c`/`arm64-asm.c`, fix STP/LDP/STR/LDR save/restore emission to use `SP` as base and restore the full stack adjustment, and fix register-shift plus `ROR` immediate/register alias handling.
Prevention: When touching ARM64 inline asm, verify both the Arm ISA docs and the backend register model in `arm64-gen.c`. Do not assume architectural register numbers match allocator register numbers, and validate changes with small object-compilation snippets plus disassembly before trying full runtime tests.
Related Files: [arm64-asm.c, arm64-gen.c, tccasm.c, tests/asm/test-asm-arm64-ext.c, tests/asm/test-asm-arm64-ext-fixed.c]

Date: 2026-03-21
Problem: The ARM64 inline-asm test executables still segfaulted after all tests printed as passed.
Root Cause: Two tests used generic `%0`/`%1` register substitution for `ldr`/`str` on 32-bit `int` variables. TCC legitimately chose 64-bit X registers, so the generated memory ops became 64-bit loads/stores and the store test overwrote the saved frame pointer in its stack frame.
Solution: Update the tests to use `%w0`/`%w1` for 32-bit load/store instructions so the emitted code uses `ldr wN`/`str wN` and does not trample the stack frame.
Prevention: For AArch64 inline asm tests, always spell the width explicitly on load/store operands when the C type is narrower than 64 bits. Generic `%0` with an `"r"` constraint is not enough to force a W register.
Related Files: [tests/asm/test-asm-arm64-ext.c, tests/asm/test-asm-arm64-ext-fixed.c]

Date: 2026-03-21
Problem: AArch64 `L` logical-immediate tests passed with literal hex immediates but failed when the same value came through extended-asm operand substitution.
Root Cause: `subst_asm_operand` printed constants through a 32-bit integer path originally, then through signed decimal only. Large 64-bit immediates with the top bit set need their full bit pattern preserved when substituted into inline asm templates.
Solution: Print ARM64 substituted constants using full-width values, and fall back to hexadecimal text for unsigned-looking 64-bit values above `INT64_MAX`. That keeps logical-immediate parsing consistent with direct hex literals.
Prevention: When adding ARM64 immediate constraints or tests, verify both direct literals and `%N` operand substitution paths. Large constants can pass parser/codegen tests in one path and fail in the other if operand formatting truncates or changes the bit pattern.
Related Files: [arm64-asm.c, tests/asm/test-asm-arm64-ext.c]

Date: 2026-03-21
Problem: `make test` failed even though `make tcc` and the targeted ARM64 asm tests passed.
Root Cause: A new helper in `arm64-asm.c` reused the name `arm64_encode_bimm64`, which already exists as a `static` helper in `arm64-gen.c`. Normal object builds keep those in separate translation units, but the test suite also compiles `tcc.c` in one-source mode, so both helpers ended up in the same translation unit and collided.
Solution: Rename ARM64 inline-asm-only helpers so they cannot collide with backend helpers in one-source builds.
Prevention: After adding new `static` helpers in target files, run `make test`, not just `make tcc`. This project still exercises one-source builds that expose cross-file static-name collisions.
Related Files: [arm64-asm.c, arm64-gen.c, Makefile, tests/Makefile]

Date: 2026-03-21
Problem: `make test` still failed after the ARM64 inline-asm implementation itself was working and the dedicated `arm64-ext-asm` test passed.
Root Cause: Two `tests/tests2` fixtures had drifted. `139_arm64_errors.test` depended on `-dt` multi-snippet mode but the Makefile never enabled it, so the harness ran the file as a normal program and printed the TCC usage text instead of per-case errors. `138_arm64_encoding.expect` also had a stale hex typo, and `139_arm64_errors.expect` still expected the old "extended inline asm is not implemented" errors.
Solution: Enable `-dt` for `139_arm64_errors.test`, update the stale expected hex value in `138_arm64_encoding.expect`, and replace the obsolete ARM64 inline-asm error expectations with current checks for an invalid operand reference and an invalid clobber register.
Prevention: When adding new `tests/tests2` multi-case files, wire `-dt` in `tests/tests2/Makefile` immediately and rerun the specific tests2 targets before trusting a full `make test` failure. If a feature graduates from "not implemented" to supported, audit any negative tests for obsolete expectations.
Related Files: [tests/tests2/Makefile, tests/tests2/138_arm64_encoding.expect, tests/tests2/139_arm64_errors.c, tests/tests2/139_arm64_errors.expect]

Date: 2026-04-04
Problem: Windows `tcc -run tcc.c ... -run ...` recursion made `tests/test2` and `tests/test3` fail, and once that was fixed the Windows x86_64 runtime still missed `atexit`/`on_exit` callbacks and destructors in `108_constructor` and `128_run_atexit`.
Root Cause: `win32/lib/crt1.c` rebuilt run arguments from the original process command line even though `run_arg_start` is relative to the currently active `-run` argv slice. That reintroduced the previous `-run tcc.c` segment on nested runs. Separately, the newer `runrt.c` callback path was wired directly into `crt1.c` while x86_64 Windows still uses `lib/runmain.c` for `-run`, so normal return and startup cleanup no longer used the same callback/destructor chain on every Windows target.
Solution: Rebuild `-run` argv from the active CRT `__argc`/`__targv` slice first, keep wildcard expansion on that slice, guard `tcc.c`'s `run_arg_start` write behind `TCC_IS_NATIVE`, route `_runtmain` through generic `atexit`/`__run_on_exit`, and keep `_tstart` on `__tcc_exit` so normal executable destructors still run.
Prevention: For Windows runtime changes, rerun both nested `make test` coverage (`tests/test2`, `tests/test3`) and the Windows-specific lifecycle tests (`tests/tests2/108_constructor`, `tests/tests2/128_run_atexit`). Do not assume ARM64's `runmain-arm64.S` shims behave the same as x86_64's `lib/runmain.c`; they exercise different cleanup paths.
Related Files: [win32/lib/crt1.c, tcc.c, lib/runmain.c, win32/lib/runmain-arm64.S, win32/lib/runrt.c, tests/Makefile, tests/tests2/108_constructor.c, tests/tests2/128_run_atexit.c]

Date: 2026-04-04
Problem: The new Windows ARM64 `-run` regression initially failed for unrelated reasons: libtcc was loading stale x86_64 helper objects, and the custom regression source could not find the Windows CRT headers it actually uses.
Root Cause: The top-level `configure` + `make` ARM64 flow rebuilds `tcc.exe`, `libtcc.dll`, and `arm64-win32-libtcc1.a`, but it does not refresh the `win32/lib/runmain.o` and `win32/lib/libtcc1.a` support files that libtcc resolves through its library path during Windows `-run`. This repo already had older x86_64 ELF support objects in `win32/lib`, so validation used the wrong architecture until `win32/build-tcc.bat -t arm64 -c clang` regenerated them. The regression harness also needed `win32/include` in addition to `include` because TCC's Windows headers provide `string.h` there.
Solution: Rebuild the Windows support tree with `win32/build-tcc.bat -t arm64 -c clang` before running ARM64 libtcc `-run` validation, and add both `.\include` and `.\win32\include` plus `.\win32\lib` to the regression harness setup.
Prevention: After changing Windows `-run` or libtcc startup code, verify the architecture of `win32/lib/runmain.o` and `win32/lib/libtcc1.a` before trusting failures. If the test is meant to exercise Windows hosted compilation, include `win32/include` explicitly instead of assuming the generic `include` tree is sufficient.
Related Files: [win32/build-tcc.bat, win32/lib/runmain.o, win32/lib/libtcc1.a, win32/test_run_arg_cleanup.c]
