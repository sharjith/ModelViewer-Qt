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
