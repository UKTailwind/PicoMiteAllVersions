# ports/host_native/tests/acceptance/

Feature-level acceptance specs. These tests are **not** run by the default
gate (`./run_tests.sh`) or the frontend smoke gate (`./run_frontend_tests.sh`) —
both only scan `tests/t*.bas` and `tests/frontend/*.bas` respectively.

Files here answer the question *"has the full feature landed?"*, not
*"does the tree still work?"*. The port workflow runs them manually as the
last gate before tagging a feature as complete.

## Current specs

- `struct_full.bas` — TYPE/STRUCT acceptance. Imported verbatim from
  `upstream/main:StructTest.bas` (UKTailwind PicoMite 6.02.02B0). 86
  numbered tests covering TYPE/END TYPE, DIM (scalar and array), field
  access (including array-of-struct, nesting, and mixed with legacy
  dotted identifier names), LOCAL/STATIC struct in sub/fun, all `STRUCT`
  sub-commands (COPY, SORT, CLEAR, SWAP, SAVE, LOAD, PRINT, EXTRACT,
  INSERT), the `STRUCT(…)` function family (FIND including regex,
  SIZEOF, OFFSET, TYPE), DIM initializer syntax, struct direct
  assignment, function returning struct, BOUND() on struct arrays, and
  random-access file I/O with struct arrays.

  Used as the spec for the TYPE/STRUCT port tracked in
  `docs/type-struct-port-plan.md`. Each phase of that plan adds a
  focused test in `ports/host_native/tests/frontend/` for its slice; when all phases
  land, this file should run clean end-to-end under both the
  interpreter and the VM.

## How to run one manually

```
cd ports/host_native
./build/mmbasic_test tests/acceptance/struct_full.bas           # compare mode
./build/mmbasic_test tests/acceptance/struct_full.bas --interp  # interpreter only
./build/mmbasic_test tests/acceptance/struct_full.bas --vm      # VM only
```

## When an acceptance spec passes

When an acceptance spec passes in compare mode, move its focused-test
counterparts out of `tests/frontend/` (they are subsumed) and leave the
acceptance file here as a permanent regression guard. The feature's
row in `docs/upstream-catchup-plan.md` flips to DONE with the commit
hash.
