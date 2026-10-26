# Larger production assembly allocation budgets

26 October 2026, `feature/JT-importer`. Owner explicitly requested both increases, differentiated error statements and a push before production testing. Build, native tests and production acceptance remain pending. No failing file was shared, and no cause beyond the reported guard rejection is claimed.

## Report and change

The owner reported another large EBOM assembly failing after exhausting LOD alternatives. The displayed final cause was `JT decoding memory limit exceeded`. The old message combined two limits and could not identify which one rejected the request. It also retained only the final alternative's failure, not every earlier LOD failure.

The production trial changes:

| Guard | Previous default | Trial default |
| --- | --- | --- |
| Cumulative charged allocation budget | 6 GiB | 16 GiB (17,179,869,184 bytes) |
| Single charged allocation cap | 256 MiB | 512 MiB (536,870,912 bytes) |

Both the importer option default and decoder scope default use 16 GiB. An explicitly supplied cumulative budget still takes precedence. The single-allocation cap remains a decoder constant. These limits charge allocation requests, including freed scratch buffers; they do not measure resident memory or reserve the stated capacity. Other format/count/decompression bounds are unchanged.

The guard now distinguishes `JT decoding single-allocation limit exceeded` from `JT decoding cumulative allocation budget exceeded`. Both messages include the requested bytes, charged bytes before the request, cumulative budget and single-allocation cap. The single-allocation condition is checked first if both conditions would reject the same request. Rejected requests do not charge the counter; cancellation still runs first; overflow-safe subtraction and nested scope restoration remain intact.

This does not fix a demonstrated leak, establish that higher limits suffice for this assembly, alter LOD selection, skip failed parts or change the owner's/Claude's committed face-group mesh work. The large model is unavailable locally, so actual peak memory, triggering request and first failing LOD remain unknown until the production test.

## Prepared regression coverage and owner gate

The accounting-only cases in `tests/jt/jt_tests.cpp` request no real large buffers. They cover matching importer/decoder defaults, accepting requests above the former 256 MiB cap, accepting exactly 512 MiB, rejecting 512 MiB plus one byte with the correct diagnostic, accepting exactly 16 GiB of accumulated requests, cumulative overflow rejection, byte fields in the cumulative diagnostic and unchanged counters after rejection. Existing cases retain nested scopes and cancellation checks.

The agent reviewed the diff and whitespace/manifest consistency only; no C++ build, native tests or application execution ran. The owner will build/install and test on production. If it still fails, capture the complete new error, especially the requested/charged/budget/cap fields. An error from a different earlier or later alternative is new evidence.

For a configured standalone JT test build, the owner can run:

```text
cmake --build <jt-test-build> --target jt_tests
ctest --test-dir <jt-test-build> -R jt_codec_tests --output-on-failure
```

## Method and provenance

Consulted the owner's screenshot and explicit trial instructions, current original allocation guard/importer defaults and original accounting tests. No model was opened or shared for this pass, and no new external source, vendor software/SDK, decoder, licence interpretation or downloaded asset was used. All changes are original bounded accounting/diagnostic work. No sample names or paths are recorded here. The implementation remains an unvalidated production trial until the owner reports the outcome.

## Production result - 26 October 2026

The owner supplied a screenshot after the raised-limit trial. The distinct cumulative guard rejected a 25,032-byte request with 17,179,851,890 bytes already charged against a 17,179,869,184-byte budget; only 17,294 bytes remained. The 536,870,912-byte single-allocation cap was not the reported trigger. The 16 GiB trial therefore did not resolve loading this assembly. This counter includes freed scratch allocations and conservative allocation charges, so the screenshot does not establish physical/resident memory consumption or a leak. Actual process-memory figures and the failing private input remain unavailable. No further budget increase or memory-accounting implementation is made in this result-recording pass.

## Runtime override follow-up - 26 October 2026

After the failed 16 GiB trial, the owner agreed to a configurable cumulative budget. The desktop viewer now reads `MODELVIEWER_JT_BUDGET_GIB` when starting each JT import. An absent variable retains 16 GiB; a whole decimal value from 1 through 256 selects that many GiB, subject to platform size limits. Empty, signed, fractional, overflowing or out-of-range values cause a clear configuration error. The 512 MiB single-allocation cap is unchanged. This is a cumulative work/allocation charge, not a RAM reservation or live-memory estimate.

Plain C++ `Options` and explicitly supplied native/probe budgets are unchanged; the desktop loader opts into `Options::fromEnvironment()`. Native regression cases are prepared for unset/valid/invalid values, preserving the original process environment after checking. Agent execution remains limited to source/static checks, not C++ build/native tests. The initial revised app build/install is required once; changing this setting afterward requires no rebuild.

For the next production trial, close existing viewer instances and launch the newly installed executable from the same PowerShell window after setting the variable:

```powershell
$env:MODELVIEWER_JT_BUDGET_GIB = '32'
& '<installed-viewer>/ModelViewer.exe'
```

For a subsequent 64 GiB trial, close the viewer, change `32` to `64` and launch it again. To restore default behaviour in that PowerShell session:

```powershell
Remove-Item Env:MODELVIEWER_JT_BUDGET_GIB -ErrorAction SilentlyContinue
```

The assignment affects processes started from that window; changing it in one shell does not update an already-running viewer or an independently launched process. Record process memory near failure as well as any complete guard diagnostic. No success for either larger trial is claimed. No new external source or asset was used for this original runtime-setting follow-up.
