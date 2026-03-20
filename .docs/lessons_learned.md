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
