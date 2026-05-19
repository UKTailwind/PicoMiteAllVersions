# TYPE / STRUCT port plan

Sub-plan for Phase A item 5 of `docs/upstream-catchup-plan.md`. TYPE/STRUCT is the centerpiece of upstream 6.02 and its full surface is larger than a single-commit port can carry; the parent plan mandates a sub-plan.

- **Upstream reference:** UKTailwind/PicoMiteAllVersions `@04f81d0`, version 6.02.02B0. Feature is guarded upstream by `#ifdef STRUCTENABLED`; we plan to drop the guard (see "Decisions").
- **Acceptance spec:** `ports/host_native/tests/acceptance/struct_full.bas` — the upstream `StructTest.bas`, 2188 lines, 86 numbered tests. When this passes under `./mmbasic_test ... --vm` and `--interp`, the port is done.
- **Our branch:** `catchup/type-struct` (off `catchup-integration`, lands per the standard catch-up workflow — **never directly to `main`**; see the "Merge target" section of `docs/upstream-catchup-plan.md`).
- **Prerequisite:** bridge-rebinding fix in `bc_bridge.c` (lands as its own commit; unlocks REDIM-in-VM at the same time, and is what lets struct memory live in `g_vartbl` without breaking VM reads after a bridged allocation).
- **Gate per phase:** host `./run_tests.sh` default compare mode green, `./build_picocalc_firmware.sh rp2040 && ./build_picocalc_firmware.sh rp2350` green, `./ports/host_wasm/build.sh` green.

## Architecture: single storage, shared by both engines

The earlier draft of this plan marked TYPE/STRUCT as interpreter-only on the same grounds that made REDIM interpreter-only — the VM's `vm->arrays[slot]` is a private per-slot store disjoint from `g_vartbl`, so a bridged command can't coherently mutate a VM-owned buffer. That reading was incomplete. `bc_bridge.c:108-151` already aliases `g_vartbl[vi].val.ia/fa/s` to `vm->arrays[i].data` on the inbound side of every bridge call, and the post-bridge sync skips arrays on the assumption that bridged commands mutate data in place. REDIM violates that assumption by reallocating; the fix is a ~20-line post-bridge rebinding loop that detects `val.* != vm->arrays[i].data` and adopts the new pointer + refreshed dims.

Once that fix is in, **struct storage living in `g_vartbl`** is no longer a problem for the VM. The VM aliases the same memory on bridge calls, the bridge's rebinding loop catches any `DIM`/`REDIM`/`STRUCT COPY array()` reallocation, and struct field access compiles natively against the aliased pointer — no per-field bridge.

Concretely:

- **Struct instance memory** is allocated by the interpreter's `cmd_dim` via `findvar(V_FIND|V_DIM_VAR)` and stored in `g_vartbl[vi].val.s`, exactly as upstream does. `PrepareProgramExt` has already populated `g_structtbl[struct_idx]` before any bytecode executes (it runs at program-load time, before `bc_source.c` runs).
- **VM slot bookkeeping** adds a `struct_idx` field to `BCSlot` so the compiler can resolve field offsets at compile time. The VM has no runtime struct registry — it trusts `g_structtbl` (populated by `PrepareProgramExt`), which is shared state.
- **VM opcodes** resolve offsets at compile time:
  - `OP_DIM_STRUCT slot, ndim` — bridges to `cmd_dim`, inherits the rebinding path.
  - `OP_LOAD_STRUCT_FIELD_I/F/S slot, offset, size` — reads `((uint8_t*)arr->data) + array_index * struct_size + offset`. Array indices are popped off the stack first when `ndim > 0`.
  - `OP_STORE_STRUCT_FIELD_I/F/S` — mirror.
  - `OP_STRUCT_FIELD_REF slot, offset, size, type` — for `pt.x` used on the LHS of `INC`/`&` or passed as a BYREF arg. Pushes a tagged reference on the VM stack.
- **`STRUCT COPY`/`SORT`/`CLEAR`/`SWAP`/`SAVE`/`LOAD`/`PRINT`/`EXTRACT`/`INSERT`** compile to `OP_BRIDGE_CMD`. They mutate the aliased `g_vartbl` memory; the post-bridge rebinding loop picks up any reallocation. Same story for `STRUCT(FIND …)` and friends on the function side — they use `OP_BRIDGE_FUN_*`.

Result: every test in `struct_full.bas` runs in both engines. No `RUN_ARGS: --interp` on any struct test.

## Feature surface (from `struct_full.bas`)

Numbered by the test in the acceptance spec. Grouped into phases for commit cadence.

| phase | tests | what it adds |
|---|---|---|
| 1 ✅ | 1, 2, 13 | TYPE/END TYPE parsing in `PrepareProgramExt`; `DIM s AS mytype` (scalar); scalar numeric/float field read+write; multiple struct types coexist — landed 2026-04-20 on `catchup/type-struct` (host 214/214 compare, both firmware builds + wasm clean, acceptance tests 1 & 13 pass both engines; t050_struct_phase1_scalar covers the phase in compare-gate) |
| 2 ✅ | 2, 31 | string members (default + `LENGTH`-qualified), ERASE cleanup path, struct packing with non-8-byte members — landed 2026-04-20 on `catchup/type-struct` (host 215/215 compare, rp2040 + rp2350 + wasm clean, acceptance tests 2 & 31 pass both engines; t051_struct_phase2_string covers the phase) |
| 3 ✅ | 3, 12, 46, 47, 48 | array-of-struct (`DIM a(n) AS type`), `a(i).x` access with constant and variable index, `BOUND()` works on struct arrays, 2D struct arrays — landed 2026-04-20 on `catchup/type-struct` (host 216/216 compare, rp2040 + rp2350 + wasm clean; 6 new VM opcodes `OP_LOAD/STORE_STRUCT_ELEM_{I,F,S}`, bridge struct-rebinding pass copies dims and stops using `findvar` which can't handle arrays without a subscript; tests 5 & 7 reserved for Phase 7 since they involve SUB arg passing) |
| 4 ✅ | 51, 52, 53, 54 | nested structs (`type` containing `type`); `a.b.c` chained access; arrays of nested members; full `arr(i).member(j).member(k)` nesting — landed 2026-04-20 on `catchup/type-struct` (host 217/217 compare, rp2040 + rp2350 + wasm clean; `ParseStructMember` now accepts nested types + array members; `ResolveStructMember` walks chained `.`/`(idx)` segments; 6 new VM opcodes `OP_LOAD/STORE_STRUCT_NESTED_{I,F,S}` for chains with intermediate index; `t053_struct_phase4_nested.bas` covers the phase) |
| 5 ✅ | 69, 70, 71, 72, 74, 74A, 74B | struct direct assignment `a = b` (single and array elements, across different arrays, with nested members, with variable index) — landed 2026-04-20 on `catchup/type-struct` (host 218/218 compare, rp2040 + rp2350 + wasm clean; interp gains a `T_STRUCT` branch in `cmd_let` that memcpys struct_size bytes from RHS; VM-side whole-struct stores bridge via `OP_BRIDGE_CMD` — `source_lhs_is_whole_struct` dry-runs the chain at compile time and routes matching statements through the interpreter's cmd_let. `t054_struct_phase5_assign.bas` covers the phase) |
| 6 ✅ | 29, 30, 32, 73 | function returning struct (`FUNCTION Foo() AS mytype`); including structs with array members; direct assignment from function return — landed 2026-04-20 on `catchup/type-struct` (host 219/219 compare, rp2040 + rp2350 + wasm clean; interp gains `CopyStructReturn` equivalent in `DefinedSubFun`, sets g_ExprStructType for the caller; VM compiler skips the body of any `FUNCTION foo(…) AS <struct>` and the whole-struct LHS bridge at the call site routes everything through the interpreter. `t055_struct_phase6_funreturn.bas` covers the phase) |
| 7 ✅ | 4, 5, 6, 7, 10, 11, 14 | struct as SUB/FUNCTION parameter (BYREF), scalar struct / array-element / whole-array passed, struct-taking function returning a scalar — landed 2026-04-20 on `catchup/type-struct` (host 220/220 compare, rp2040 + rp2350 + wasm clean; VM compiler tags SUB/FUN whose params/return involve a struct as `.bridged`; `source_stmt_references_bridged_subfun` catches any statement mentioning a bridged fn and routes the whole line through `OP_BRIDGE_CMD`; bridge's `bc_bridge_call_cmd` dispatches user-defined SUB calls by name (FindSubFun + DefinedSubFun) and drives the body via a scoped `ExecuteProgram` with a double-NUL sentinel. Added `BCSubFun.bridged` + a TYPE-prescan pass so predeclare can classify params against a populated `g_structtbl`. `t056_struct_phase7_subarg.bas` covers the phase) |
| 8 ✅ | 19, 20, 22 | `LOCAL` struct in sub/fun, `LOCAL` struct array, memory cleanup across many calls (+`STATIC` struct) — landed 2026-04-20 on `catchup/type-struct` (host 221/221 compare, rp2040 + rp2350 + wasm clean; predeclare body-scan (`source_local_line_has_struct_as`) flags any SUB/FUN whose body declares `LOCAL` / `STATIC` `… AS <struct>` as `.bridged`, and `source_compile_sub` / `source_compile_function` now honour the pre-existing flag to skip body compilation — calls already route through `OP_BRIDGE_CMD` via Phase 7's `source_stmt_references_bridged_subfun`. Also fixed a pre-existing port omission: `cmd_dim`'s STATIC branch was missing `T_STRUCT` in the `(T_STR | T_STRUCT)` mask, so STATIC struct aliased `val.i` instead of `val.s` and read stale heap. `t057_struct_phase8_local.bas` covers LOCAL scalar, LOCAL array, memory stress, shadowing, FUNCTION with LOCAL struct, and STATIC struct retention. Test 21 (`LOCAL p As Point = (1,2)` initializer) deferred to Phase 17 where the DIM-initializer parser lands. |
| 9 ✅ | 8, 9, 33, 34, 35, 36, 49, 50 | `STRUCT COPY` (single, mixed types, `COPY array() TO array()` preserving extra elements), `STRUCT CLEAR` (single + array), `STRUCT SWAP` (single + array elements) — landed 2026-04-20 on `catchup/type-struct` (host 222/222 compare, rp2040 + rp2350 + wasm clean; new `cmd_struct` in Commands.c ports upstream's COPY/CLEAR/SWAP arms verbatim (Commands.c:6711-7150 @04f81d0) and errors on unknown subcommand so later phases replace the error arm rather than reshuffle; registered in AllCommands.h as `"Struct"`. VM has no native handling — `Struct …` statements reach the interpreter via the generic statement-bridge. Acceptance tests 1-14 now pass under interp; later tests stop at 15 which needs Phase 17 initializer. `t058_struct_phase9_copy_clear_swap.bas` covers the phase) |
| 10 ✅ | 23, 24, 25, 26, 27, 28 | `STRUCT SORT` by integer / string / float member; reverse; case-insensitive; empty-strings-at-end — landed 2026-04-20 on `catchup/type-struct` (host 223/223 compare, rp2040 + rp2350 + wasm clean; new SORT arm in `cmd_struct` ports upstream's shell-sort verbatim from Commands.c:6819-7058 (@04f81d0) with its flag bitfield (bit0 reverse, bit1 case-insensitive, bit2 empty-at-end). Bridged via the generic statement-bridge. **Also ported an upstream `findvar` fix:** empty-array struct-member access (`sa().x`, used by SORT / EXTRACT / INSERT / FIND / COPY array()) was bailing with "Expected structarray().membername syntax" — our findvar skipped the `()` but didn't look for a trailing `.member`. Mirrored upstream MMBasic.c:4215's post-`()`-dot walk. `t059_struct_phase10_sort.bas` covers the phase) |
| 11 ✅ | 41, 64, 65, 66, 67, 75 | `STRUCT SAVE` / `STRUCT LOAD` to/from file; individual elements; array-of-struct; round-trip; random-access with SEEK — landed 2026-04-20 on `catchup/type-struct` (host 224/224 compare, rp2040 + rp2350 + wasm clean; new SAVE/LOAD arms in `cmd_struct` ported from Commands.c:7151-7310 (@04f81d0) — byte-level via `FilePutChar`/`FileGetChar` rather than upstream's unported `FilePutData`/`FileGetData` bulk primitives, which is portable across device FATFS, device LittleFS, host POSIX with zero HAL changes; bulk primitives can land later if profiling finds a hot spot. Added `VM_FILE_MODE_RANDOM` to vm_sys_file.{c,h} + bc_source.c's OPEN parser. **File-table bridge-fix:** the VM's `vm_files[]` is disjoint from the interpreter's `FileTable[]`; STRUCT SAVE/LOAD bridge to the interpreter and can't see VM-opened files. New `fe.uses_struct_file_io` flag set by a substring prescan — when a program uses STRUCT SAVE/LOAD, OPEN/CLOSE/SEEK also route through the bridge so a single file table owns all I/O. PRINT# / INPUT# keep their VM path; programs mixing both within one file number are out of scope (tradeoff documented in the gating comment). `t060_struct_phase11_save_load.bas` covers the phase incl. SEEK-based reverse read) |
| 12 ✅ | 43, 44, 45 | `STRUCT PRINT` (single, element, whole array) — landed 2026-04-20 on `catchup/type-struct` (host 225/225 compare, rp2040 + rp2350 + wasm clean; new PRINT arm in `cmd_struct` ported from Commands.c:7312-7533 (@04f81d0) verbatim — pretty-print of type name, then each member with `  .NAME = value` (ints, floats, strings, array members) + one level of nested-struct recursion. Bridged. `t061_struct_phase12_print.bas` covers the phase) |
| 13 ✅ | 77, 78, 79, 80, 81, 82, 83, 84 | `STRUCT EXTRACT member → flat array`; `STRUCT INSERT flat array → member`; all three member types; preservation of non-extracted members — landed 2026-04-20 on `catchup/type-struct` (host 227/227 compare, rp2040 + rp2350 + wasm clean; new EXTRACT/INSERT arms in `cmd_struct` ported from Commands.c:7534-7772 (@04f81d0). **Three supporting changes:** `fe.uses_struct_extract_insert` prescan flag forces any DIM of a sized non-struct array to bridge, so int/float arrays live in g_vartbl's contiguous layout. New first-time adoption pass in `bc_bridge.c` walks g_vartbl by suffix-stripped name and aliases the pointer into `vm->arrays[i].data` so subsequent VM opcodes see it. **T_STR arrays deliberately excluded from adoption** — VM stores `DIM s$(n)` as `BCValue[]` (pointer-per-element) vs interpreter's contiguous `(len+1)*N`, so bridged EXTRACT/INSERT on string members corrupts memory. Tests 79 and 83 (string cases) run as `t063_struct_phase13_extract_insert_str.bas` with `RUN_ARGS: --interp`; int/float (77, 78, 80, 81, 82, 84) in `t062` in compare mode. Unifying the two string-array layouts is a follow-up outside Phase 13's scope) |
| 14 ✅ | 37, 38, 39, 40, 42 | `STRUCT(FIND arr().member, value [, start])`; int/float/string match, not-found returns -1, start parameter iterates duplicates — landed 2026-04-20 on `catchup/type-struct` (host 228/228 compare, rp2040 + rp2350 + wasm clean; new `fun_struct` in Functions.c with the non-regex branch of upstream's FIND arm (Commands.c:8076-8297 @04f81d0). Registered as `"Struct("` with `T_FUN | T_INT` (matches upstream — STRUCT always returns int). Bridged via `OP_BRIDGE_FUN_I`. Regex branch (argc==7) errors with "Phase 15 pending" until Phase 15 brings in the regex back-end. `t064_struct_phase14_find.bas` covers the phase) |
| 15 ✅ | 42A, 42B, 42C, 42D, 42E, 42F | `STRUCT(FIND)` with regex (anchors `^` / `$`, char classes `[A-D]`, digit `\d+` / `\s` / `\w`, wildcards) — landed 2026-04-20 on `catchup/type-struct` (host 229/229 compare, rp2040 + rp2350 + wasm clean; imported upstream's tiny-regex-c-derived `re.c` + `re.h` (1501 + 73 lines) — self-contained, public-domain descendant of Rob Pike's regex. Wired into host Makefile, CMakeLists.txt (PICOMITE_BASE_COMMON_SOURCES), host/Makefile.wasm. fun_struct FIND's argc==7 branch now runs the pattern via `re_match(pattern, text, &len)` against each string member, writing `matchlen` through the size variable (int or float). Kept our legacy glibc-derived `xregex.c` in place; to avoid macro collision in Functions.c, `#undef re_match` locally before `#include "re.h"`. `t065_struct_phase15_find_regex.bas` covers the phase) |
| 16 ✅ | 60, 61, 62, 63, 63a, 63b, 63c, 63d, 85, 85b, 85c, 85d | `STRUCT(SIZEOF typename)`, `STRUCT(OFFSET typename, member)`, `STRUCT(TYPE typename, member)`; case-insensitive; variable typename — landed 2026-04-20 on `catchup/type-struct` (host 230/230 compare, rp2040 + rp2350 + wasm clean; three arms added to `fun_struct`, ported verbatim from Commands.c:8298-8408 (@04f81d0). All three walk `g_structtbl` by case-insensitive name; TYPE looks up the member via `FindStructMember` and returns the base type bitmask (T_INT=4, T_NBR=1, T_STR=2) consistent with MMBasic.h. Bridged via `OP_BRIDGE_FUN_I`. `t066_struct_phase16_sizeof_offset_type.bas` covers the phase) |
| 17 ✅ | 15, 16, 17, 18, 21 | `DIM s AS Point = (1, 2)` scalar initializer; string-member initializer; array-of-struct initializer; mixed-type init; `LOCAL … AS <struct> = (…)` initializer (test 21 deferred from Phase 8) — landed 2026-04-20 on `catchup/type-struct` (host 231/231 compare, rp2040 + rp2350 + wasm clean; new T_STRUCT branch inserted into `cmd_dim`'s init block ported from Commands.c:6355-6494 (@04f81d0). Walks `g_structtbl[struct_idx]->members` in declared order, calls `SetValue` per member (handles T_INT/T_NBR/T_STR), iterates array members, iterates whole-struct-array. LOCAL initializer rides the same path because cmd_dim handles LOCAL too — the bridging Phase 8 set up forwards to it. `t067_struct_phase17_dim_init.bas` covers the phase) |
| 18 ✅ | 55, 56, 57, 58, 59 | legacy-dotted-name coexistence (`Dim My.Variable%`, dotted subs, dotted functions, dotted arrays); mixed dotted identifiers with struct-member access in the same expression — landed 2026-04-20 on `catchup/type-struct` (host 232/232 compare; **zero source changes** — Phase 1's `FindStructBase` already disambiguates at findvar time: if the prefix before the dot is a registered struct, treat as `struct.member`; otherwise fall through to the standard dotted-identifier lookup. `t068_struct_phase18_dotted.bas` covers dotted scalar/float/string vars, dotted SUB, dotted FUNCTION, dotted array, and mixed usage in expressions) |
| 19 ✅ | 76 | graphics commands taking struct-array members (e.g. `BOX pts(i).x, pts(i).y, …`) — landed 2026-04-20 on `catchup/type-struct` (host 233/233 compare; **zero source changes** — Phase 3/4's struct-array-element.member compile path and `findvar` arm already handle these expressions in both interp and VM argument-evaluation. `t069_struct_phase19_graphics.bas` covers struct-array-member read, arithmetic, and intact-after-read. Note: interp's user-SUB argument path still errors on `boxes(i%).x` as a BYREF arg with "Incompatible type" — a narrower limitation than Phase 19 needs (PRINT / BOX work, SUB arg doesn't), separate follow-up) |

19 phases sounds like a lot; most are small after phases 1-4. Phases 1-3 build the scaffolding (typedefs, globals, `PrepareProgramExt` pass, `findvar` dot-notation hook, compile-time offset resolution in `bc_source.c`). Phases 4-18 add capabilities that mostly don't touch that scaffolding.

## Per-phase VM vs. interpreter work

For each phase the interpreter-side port is a straight read-and-adapt from upstream (anchors in `docs/upstream-catchup-plan.md` and in the scope section below). The VM work per phase is much smaller because most of it rides on shared storage:

| phase | interp work | VM work |
|---|---|---|
| 1 | TYPE scan in `PrepareProgramExt`; `FindStructType`; `ParseStructMember`; `cmd_type`/`cmd_endtype`; `CheckIfTypeSpecified` hook in `cmd_dim`; `ResolveStructMember` (scalar-only first cut); `FindStructBase`; `s_structdef`/`s_structmember` typedefs; `T_STRUCT` flag; new globals | `BCSlot` gains `struct_idx`; `bc_source.c` compiles `DIM v AS type` → emits `OP_DIM_STRUCT`; compiles `v.field` → `OP_LOAD_STRUCT_FIELD_*`/`OP_STORE_STRUCT_FIELD_*` with compile-time offset lookup in `g_structtbl`; three new VM opcodes |
| 2 | String members in `ParseStructMember`; `erase()` walks struct members freeing T_STR | none — same opcodes handle string offset; VM just emits `OP_LOAD_STRUCT_FIELD_S` |
| 3 | array-of-struct allocation in `cmd_dim`; `ResolveStructMember` handles `a(i).x` | `OP_LOAD_STRUCT_FIELD_*` already takes array-index params; bc_source.c parses the optional index |
| 4 | `ResolveStructMember` walks chained `.` and `(…)` up to MAX_STRUCT_NEST_DEPTH | bc_source.c walks the chain at compile time, accumulates offset into a single compile-time constant; still one opcode emitted |
| 5 | `cmd_subfun`/assignment path handles `a = b` for T_STRUCT (memcpy struct_size bytes) | `OP_STRUCT_ASSIGN dst_slot, dst_idx, src_slot, src_idx, size` or bridge — leaning toward bridge since it's used less than field access |
| 6 | Function-return-struct machinery (`CopyStructReturn`) | new opcode or bridge — TBD after profiling; start with bridge |
| 7 | SUB/FUNCTION argument passing for T_STRUCT; BYREF via pointer | bc_source.c emits argument-passing as pointer-to-struct-memory (new stack tag, analogous to existing array-ref tag) |
| 8 | `LOCAL s AS type` / `STATIC s AS type` in `cmd_dim` local path | `OP_DIM_LOCAL_STRUCT`; local-slot version of load/store |
| 9 | `cmd_struct` COPY / CLEAR / SWAP | bridged; no new VM opcodes |
| 10 | `cmd_struct` SORT | bridged |
| 11 | `cmd_struct` SAVE/LOAD with file I/O; random-access GET/PUT | bridged |
| 12 | `cmd_struct` PRINT | bridged |
| 13 | `cmd_struct` EXTRACT / INSERT | bridged |
| 14 | `fun_struct` FIND | bridged via `OP_BRIDGE_FUN_I` |
| 15 | regex in fun_struct FIND — uses existing regex (confirm it handles the upstream pattern subset; swap to upstream's `re.c` if not) | same as 14 |
| 16 | `fun_struct` SIZEOF / OFFSET / TYPE | bridged |
| 17 | `cmd_dim` initializer parsing | bc_source.c parses and emits a store sequence after `OP_DIM_STRUCT`; no new VM opcodes |
| 18 | Tokenizer / `findvar` disambiguation of legacy dotted names from struct-member references | bc_source.c mirrors the same disambiguation: if `a` is a struct slot, parse `a.b` as field access; otherwise treat the whole `a.b` as a single identifier |
| 19 | Graphics-command plumbing accepts struct-array-member "arrays" | none — existing array-argument path works once aliasing is in |

Total new VM opcodes across the port: roughly 6-8 (DIM_STRUCT, LOAD_STRUCT_FIELD_I/F/S, STORE_STRUCT_FIELD_I/F/S, FIELD_REF, DIM_LOCAL_STRUCT). Every other struct operation bridges.

## Upstream anchors

These are the code coordinates — the porter reads upstream first per the parent plan's invariants.

| concern | file:line | notes |
|---|---|---|
| TYPE block scan | `MMBasic.c:949-1157` | inside `PrepareProgramExt`; walks tokenized program, calls `ParseStructMember` per member |
| `cmd_type` | `Commands.c:6663-6689` | runtime body just skips to `END TYPE` |
| `cmd_endtype` | `Commands.c:6690-6703` | error if reached directly |
| `cmd_struct` | `Commands.c:6704-7400+` | COPY / SORT / CLEAR / SWAP / SAVE / LOAD / PRINT / EXTRACT / INSERT sub-commands |
| `fun_struct` | `Commands.c:8060+` | `STRUCT(FIND …)`, `STRUCT(SIZEOF …)`, `STRUCT(OFFSET …)`, `STRUCT(TYPE …)` |
| `ParseStructMember` | `Commands.c:7776-7980` | member name + type + optional dims + alignment |
| `FindStructType` | `Commands.c:7986-8010` | name → struct index |
| `FindStructMember` | `Commands.c:8014-8060` | struct index + name → offset / type / size |
| `ResolveStructMember` | `MMBasic.c:3371-3670` | chained dot + array index; sets `g_StructMemberType/Offset/Size`; returns pointer |
| `FindStructBase` | `MMBasic.c:3686-3750` | dot-notation entry in `findvar` |
| `CheckIfTypeSpecified` + DIM init | `Commands.c:6286-6550` | resolves `AS mytype`; allocates `struct_size × num_elements`; initializer `DIM s AS Point = (x, y, …)` |
| alignment | `Commands.c:7939`, `MMBasic.c:1125-1130` | 8-byte align for INTEGER/FLOAT/nested; trailing pad for array elements |
| ERASE string-member cleanup | `Commands.c:5785` | walks members, frees T_STR |
| REDIM with struct arrays | `Commands.c:5612-5705` | `g_StructArg = structIdx` before `findvar`; strips ` AS structtype`; PRESERVE copies struct data |
| dots-in-names parsing | `MMBasic.c:3879-3920` (inside findvar) | disambiguate legacy dotted name from struct field reference |

## Decisions

1. **`STRUCTENABLED` always-on.** Drop the guard; matches parent plan invariant #4 (no new hardware `#ifdef` gates in core interpreter files). One fewer compile variant to maintain.
2. **Alignment: 8-byte.** Upstream's choice; valid across rp2040, rp2350, host (x86_64/arm64), wasm32.
3. **Capacities: upstream's defaults** — `MAX_STRUCT_TYPES = 32`, `MAX_STRUCT_MEMBERS = 16`, `MAX_STRUCT_NEST_DEPTH` whatever upstream uses. Raise only if an acceptance test demands it.
4. **Storage: `g_vartbl`.** Struct instances and struct arrays allocate through the interpreter's standard `cmd_dim` path; VM accesses via alias. Depends on the bridge-rebinding fix landing first.
5. **`vm->arrays[slot]` for struct variables becomes "pointer-only".** `data` aliases `g_vartbl[vi].val.s`; `elem_type = T_STRUCT`; `dims[]` shadows `g_vartbl[vi].dims`. Rebinding loop keeps the pointer current. No separate BCValue storage for struct fields.
6. **Field offsets resolved at compile time.** `bc_source.c` reads `g_structtbl` during compilation (which is safe because `PrepareProgramExt` runs first — confirmed by checking the FRUN command flow). If the user redefines a TYPE mid-program (upstream doesn't support this either), we error.
7. **Bridge for aggregate-mutation commands.** COPY/SORT/SWAP/SAVE/LOAD/PRINT/EXTRACT/INSERT all bridge — they are rare in hot paths and mutate memory that's already aliased. Not worth native opcodes.

## Risks and gotchas

1. **`g_vartbl[idx].size` overload.** When `type & T_STRUCT`, `size` holds the struct-type index, not string length. Audit every `.size` reader in the tree (grep for the pattern) and guard each against `T_STRUCT`.
2. **`NAMELEN_STATIC` collision.** Static variables tag `namelen` with a bit flag; dot-notation resolver must mask it before name comparison.
3. **REDIM+struct.** Phase 3 must wire `g_StructArg` into our REDIM port. Cross-link the two ports so the REDIM author is aware.
4. **ERASE string-member free.** Phase 2. Program reload must free struct strings without leaks.
5. **Tokenizer: `End Type` as two tokens.** Command table is longest-match; confirm upstream's ordering and mirror it exactly.
6. **Alignment on wasm32.** Emscripten `wasm32` uses 4-byte `size_t` but 8-byte `long long`. 8-byte alignment is correct; verify with a mixed-member struct in Phase 2 tests.
7. **Program-reload / `NEW` cleanup.** `PrepareProgramExt` heap-allocates `g_structtbl[]` entries; reload must free them. Find upstream's cleanup site (likely `ClearRuntime` or a dedicated `ClearStructs`) and mirror.
8. **Legacy dotted-name disambiguation (Phase 18).** This is where the whole port is most likely to regress working programs. Our tree probably already supports `Dim My.Var%`. The resolver change for struct notation has to leave that path intact. Strategy: disambiguation happens at `findvar` lookup time — if the prefix before the `.` is a declared struct variable, treat the rest as a field; otherwise treat the whole thing as a legacy dotted identifier.
9. **Compile-time vs. runtime for VM opcodes.** `bc_source.c` resolves field offsets via `g_structtbl` at compile time. If TYPE is declared *after* a `DIM v AS type` that uses it (legal in MMBasic since `PrepareProgramExt` does a full scan first), our compile-time lookup still works because `PrepareProgramExt` has populated `g_structtbl` before the bytecode compiler runs.
10. **`fun_struct FIND` regex.** Our existing regex module (`xregex.c` / `xregex2.h`) may not support the exact syntax upstream's tests use (`\d+`, `[A-D]`, `^`). Phase 15 starts with an audit — if our regex handles the cases, port `fun_struct FIND` as-is; if not, either limit feature scope or swap in upstream's `re.c` (tier-2 catchup item, already on the parent plan).

## Test cadence

Each phase adds one focused test in `ports/host_native/tests/frontend/t0NN_struct_*.bas` covering exactly that phase's slice. The phase lands when:

1. Focused test passes under the default gate (compare mode).
2. The corresponding test numbers in `ports/host_native/tests/acceptance/struct_full.bas` pass when that file is run manually with `./mmbasic_test tests/acceptance/struct_full.bas`. (We check the numbered sections listed in the phase table above; later phases may cause earlier numbered tests to regress if scaffolding assumptions change — running the full acceptance file catches that.)
3. All firmware + wasm builds green.

Focused-test filenames reserved up front (adjust numbers if other tests land first):

- `t050_struct_phase1_scalar.bas`
- `t051_struct_phase2_string.bas`
- `t052_struct_phase3_array.bas`
- `t053_struct_phase4_nested.bas`
- `t054_struct_phase5_assign.bas`
- `t055_struct_phase6_funreturn.bas`
- `t056_struct_phase7_subarg.bas`
- `t057_struct_phase8_local.bas`
- `t058_struct_phase9_copy_clear_swap.bas`
- `t059_struct_phase10_sort.bas`
- `t060_struct_phase11_save_load.bas`
- `t061_struct_phase12_print.bas`
- `t062_struct_phase13_extract_insert.bas`
- `t063_struct_phase14_find.bas`
- `t064_struct_phase15_find_regex.bas`
- `t065_struct_phase16_sizeof_offset_type.bas`
- `t066_struct_phase17_dim_init.bas`
- `t067_struct_phase18_dotted_names.bas`
- `t068_struct_phase19_graphics_args.bas`

## Exit criteria

- `ports/host_native/tests/acceptance/struct_full.bas` passes end-to-end under both `--interp` and `--vm` modes, i.e. `./mmbasic_test tests/acceptance/struct_full.bas` reports no `FAIL` lines in either engine.
- All focused-test counterparts in `tests/frontend/` pass in the default compare gate.
- Firmware (rp2040, rp2350) produces `.uf2` without new warnings.
- WASM build produces `picomite.{mjs,wasm}` without new warnings.
- `docs/upstream-catchup-plan.md` updated: Phase A item 5 DONE with commit range.
- Catch-up milestone complete on `catchup-integration`. Device-validation step per parent plan — flash both rp2040 and rp2350 `.uf2`s on real hardware, run acceptance programs on the PicoCalc — clears before any `main` merge. Tag `v6.01-parity-plus-lang` applied only after that.
- Consider: once this and REDIM are both landed, dust off the `FRUN` documentation to remove any "not supported in VM mode" language for array/struct operations.

## Out of scope

- Struct-type inheritance (not an upstream feature).
- Compile-time struct literal constants (`CONST p AS Point = (1,2)` — upstream only supports CONST on scalars).
- Anything in upstream's `STRUCTENABLED` block not listed in the phase table above — scope it first, don't port cold.
