The inherited-sheet change is under development for issue
https://github.com/UnofficialCrusaderPatch/ucp_gmResourceModifier/issues/6.
No release or rendered custom-projectile acceptance is claimed yet.

Owner review: base 019039a. `ColorAdapter::detouredLoadGmFiles` owns the one native
load call, replacer initialization and queued replacements. `SetGm` owns original
snapshots and active replacement references; `FreeGm1Resource` rejects pinned
resources. Empty slots have const `Replacer::firstImageIndex == -1`, so they cannot
be allocated after that initialization without changing the lifecycle. The new
reservation batch is integrated before construction; there is no second loader,
frame hook or resource cache. The existing UCP cached AOB owner resolves bindings.
The expanded loader signature identifies its native capacities, renderer fields,
first-image store and per-sheet count stride. Its caller's decoded target must
agree before any hook write. The new native arrays come from these instructions.

Local verification, 12 September 2026:

- Release Win32 DLL builds with MSVC v143, real framework ucp3.h / UCP import
  library and Lua 5.4.6. Existing compiler warnings remain in old loader/SetGm Lua
  conversions; the reservation wrappers reject wide Lua IDs instead of wrapping.
- `reservations.cpp` links the production resource implementation to a small
  test host. Only UCP logging and the native disk loader are substituted. The real
  admission, native metadata writes, pixel preparation, queued SetGm, snapshots,
  resource pins, reset and free paths execute. Six scenarios pass: shared-resource
  success, type mismatch, sheet exhaustion, image exhaustion, occupied trailing
  slot and invalid first-image index. Tests assert failed admission leaves reserved
  metadata/indices untouched while old texture replacements still execute.
- `python tests/test_bindings.py -v` executes production Lua with the actual
  framework AOB/cache implementation against normal Crusader and Extreme. With
  `SHC_VARIANT_DIRS` it also passes the official PL/EFIGS fixtures for both families.
  Each signature is unique in all six files. Changed loader capacity/opcode,
  occupied caller and misleading earlier loader copy fail before native writes.
  The official fixtures' native sections equal the reference family; their file
  hashes differ. These are fixture/binding checks, not live language acceptance.

Live owner initialization also passed on normal Crusader (PID25172) and Extreme
(PID36156), using framework 3.0.7 developer build 77c6acc, owner source cf8c2b7 and
the real DLL. `native-init.lua` loaded native missile/cow/brazier/rock-chip sheets
(types 2, 6 and 1), reserved five slots and verified IDs 207..211, pre-init pins,
shared active references, queued texture replacement, inherited resets and final
free through public APIs. The expected refused-free/queued-SetGm warnings were
the only warnings; no ERROR/FATAL occurred. Both games remained responsive and
closed normally; desktop released at 20:45:55 CEST. No game/save was loaded or
written. The test installation's original module/config were restored exactly.

Still required: real custom-projectile rendering and consumer initialization with
configured variants on both families; failure admission through the real framework; multiplayer/save/replay
layout identity; malformed resource validation/content identity in this owner;
runtime diagnostic locale handling; supported-variant matrix review; CI/review
and normal merge. Passing host tests does not close these gates.

Build/run the host suite with `cmake -S tests -B build-tests -A Win32`,
`cmake --build build-tests --config Release` and
`ctest --test-dir build-tests -C Release --output-on-failure`. It uses the same
pinned Lua-source test-host pattern as Recorder's compatibility checks. Release
assertions remain enabled in the test. The stub include directory is never used
in the production DLL build. Native fixture checks stay local; CI downloads no
game executables or artwork.
