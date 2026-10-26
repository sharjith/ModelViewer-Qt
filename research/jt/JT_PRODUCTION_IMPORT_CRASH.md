# Large-assembly post-decode crash diagnostics

26 October 2026, `feature/JT-importer`. This is a diagnostic and exception-containment gate for an owner-only production assembly. The private model was not shared, so the crash cause remains unproven and no model-specific fix is claimed.

## Production observation

With `MODELVIEWER_JT_BUDGET_GIB=64`, JT reading passed the cumulative allocation guard. The owner reported that more than 30,000 meshes were processed before the application terminated. The last ordinary application log entry identified scene index 30,950 as stored. Windows reported `ucrtbase.dll` and exception code `0xC0000409`.

That code is Windows fail-fast status `STATUS_STACK_BUFFER_OVERRUN`, but Microsoft documents that the mechanism is also used for immediate process termination after other unrecoverable runtime failures. An unhandled C++ exception can call `std::terminate`, whose default path calls `abort`. The status and module therefore do not establish a stack overwrite, heap corruption or out-of-memory condition without the fast-fail subcode or a crash dump.

The ordinary application logger is asynchronous. Its last delivered line is evidence that processing reached that point, not proof that the fault occurred on the next statement.

## Prepared changes

The loader now places an exception boundary around post-read model processing and reports its current stage, scene index and completed occurrence count. Viewer mesh construction has the same boundary. A failure rolls back meshes attached during the current import instead of allowing a C++ exception to escape the worker or GUI event dispatch. `SceneMesh` construction retains temporary ownership until all builder steps succeed, and runtime display registration is transactional before ownership transfers.

Two full geometry-buffer copies in the large-model handoff were reduced: the worker reserves its known occurrence count, moves completed records into its store, and transfers that store to the viewer after the worker stops. Progressive batches still require their independent payload while the worker retains final scene metadata; this pass does not claim a zero-copy progressive pipeline.

The opt-in trace records synchronously flushed checkpoints independently of the normal queued logger. Each line contains a sequence, time, thread, stage, scene index, completion counts and, on Windows, process private bytes and working-set bytes. It never writes geometry. Exception text can contain a source path and must be reviewed or redacted before sharing. The trace is bounded to approximately 32 MiB and wraps to retain recent events; sequence numbers continue across a wrap.

`privateBytes` and `workingSetBytes` are operating-system process counters. They are different from the JT decoder's cumulative allocation charge, and neither field reports GPU memory.

The installed application creates no trace unless `MODELVIEWER_IMPORT_DIAGNOSTICS=1` is set. When enabled, the file is written below the application's normal data directory as `logs/import-trace-<pid>-<time>.log`; on the owner's Windows installation this is the existing `%APPDATA%\Sharjith N\ModelViewer\logs` folder.

An opt-in process terminate hook records an uncaught standard C++ exception and then chains the previously installed handler. It is diagnostic only. Structured exceptions, direct fail-fast calls, heap-corruption termination and failures that prevent the process from writing can bypass it. A flushed stdio record is not equivalent to a crash dump and is not guaranteed to survive an operating-system or storage failure.

## Owner build and validation gate

The agent performed source and whitespace checks only. It did not build the viewer or run native tests. The owner should build/install once, then run the focused trace test if the test tree is enabled:

```powershell
cmake --build <viewer-build> --target import_trace_tests
ctest --test-dir <viewer-build> -R import_trace_tests --output-on-failure
```

Before the production retry, close existing viewer processes. Launch the newly installed executable from one PowerShell window:

```powershell
$env:MODELVIEWER_JT_BUDGET_GIB = '64'
$env:MODELVIEWER_IMPORT_DIAGNOSTICS = '1'
& '<installed-viewer>/ModelViewer.exe'
```

Load the production assembly once. If it succeeds, record completion and peak process memory. If it reports a controlled error or terminates, retain the newest `import-trace-*.log` and the full dialog/Event Viewer details. The final trace stages distinguish worker conversion/storage/batch handoff, viewer mesh construction/attachment, scene graph creation, scene copy and scene merge.

Also smoke-check one small model in progressive mode, one in non-progressive mode and cancellation of a native CAD import. Acceptance requires unchanged successful display and an atomic rollback on failure/cancel; this document does not infer those results.

To restore the default environment in that shell:

```powershell
Remove-Item Env:MODELVIEWER_IMPORT_DIAGNOSTICS -ErrorAction SilentlyContinue
Remove-Item Env:MODELVIEWER_JT_BUDGET_GIB -ErrorAction SilentlyContinue
```

## Evidence and limits

This pass consulted the owner's reported production result and current original loader, viewer, runtime-store, mesh-builder and logger sources. It did not inspect or execute the private assembly. No external decoder, vendor tool/SDK, sample, downloaded asset, disassembly or third-party implementation was used.

Microsoft references consulted for interpreting the Windows status and C++ termination path:

- [__fastfail intrinsic](https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail?view=msvc-170), accessed 26 October 2026.
- [Unhandled C++ exceptions](https://learn.microsoft.com/en-us/cpp/cpp/unhandled-cpp-exceptions?view=msvc-170), accessed 26 October 2026.
- Raymond Chen, [The magic security cookie: STATUS_STACK_BUFFER_OVERRUN](https://devblogs.microsoft.com/oldnewthing/20230731-00/?p=108505), accessed 26 October 2026.

The new implementation is original diagnostic and exception-safety work. These references explain platform behavior; they do not supply implementation code or establish the private crash's cause. This records method and qualification limits, not legal clearance.
