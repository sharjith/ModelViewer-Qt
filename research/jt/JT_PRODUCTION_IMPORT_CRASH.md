# Large-assembly post-decode crash diagnostics

26 October 2026, `feature/JT-importer`. This is a diagnostic and exception-containment gate for an owner-only production assembly. The private model was not shared, so the crash cause remains unproven and no model-specific fix is claimed.

## Production observation

With `MODELVIEWER_JT_BUDGET_GIB=64`, JT reading passed the cumulative allocation guard. The owner reported that more than 30,000 meshes were processed before the application terminated. The last ordinary application log entry identified scene index 30,950 as stored. Windows reported `ucrtbase.dll` and exception code `0xC0000409`.

After exception containment was installed, the same private assembly produced a controlled `std::bad_alloc` during viewer-mesh creation instead of terminating the process. The opt-in trace placed the failure near viewer scene index 19,982, with process private bytes at approximately 53.2 GiB and working set at approximately 39.7 GiB. Rollback then reduced private bytes to approximately 3 GiB. These measurements show that the post-decode conversion/viewer overlap was material; they do not establish the remaining memory cost of later scene-copy and scene-merge stages because this run did not reach them.

After the geometry-lifetime changes described below, a second owner run again reached a controlled `std::bad_alloc` in viewer-mesh creation, this time near scene index 29,834. The trace showed approximately 62.2 billion private bytes and 45.6 billion working-set bytes immediately before rollback, then approximately 5.55 billion private bytes after rollback. Scene indices are source identifiers and are not a monotonic mesh counter, but the later failure and larger resident viewer scene show that removing transient full-model copies materially extended the load. The remaining growth is therefore dominated by geometry retained by already-created viewer meshes rather than the released conversion batches.

That code is Windows fail-fast status `STATUS_STACK_BUFFER_OVERRUN`, but Microsoft documents that the mechanism is also used for immediate process termination after other unrecoverable runtime failures. An unhandled C++ exception can call `std::terminate`, whose default path calls `abort`. The status and module therefore do not establish a stack overwrite, heap corruption or out-of-memory condition without the fast-fail subcode or a crash dump.

The ordinary application logger is asynchronous. Its last delivered line is evidence that processing reached that point, not proof that the fault occurred on the next statement.

## Prepared changes

The loader now places an exception boundary around post-read model processing and reports its current stage, scene index and completed occurrence count. Viewer mesh construction has the same boundary. A failure rolls back meshes attached during the current import instead of allowing a C++ exception to escape the worker or GUI event dispatch. `SceneMesh` construction retains temporary ownership until all builder steps succeed, and runtime display registration is transactional before ownership transfers.

The follow-up memory change separates geometry lifetime from the lightweight import summary. In progressive mode, each converted record moves directly into its bounded handoff batch; the worker no longer retains another full-model `AssImpMeshData` geometry store. In non-progressive mode, the transferred full-model batch is consumed one record at a time and each moved-from source record is destroyed before hierarchy copying and scene merging. `SceneMesh` also moves its by-value constructor buffers into live storage, eliminating a redundant temporary vertex/index/texture copy.

The next retained-memory change makes the unmorphed rest-pose array lazy. Ordinary rigid meshes and skin-only meshes now keep one interleaved `Vertex` array; a separate `_baseVertices` array is allocated only when actual morph targets are installed. This removes one 128-byte CPU record per vertex from the common static-assembly path. Morph blending, morph-aware cloning and ray-tracing snapshots continue to use the separate rest pose. Rigid cloning and fragment extraction use the live array as the equivalent rest pose. This does not reduce the live vertex array, GPU-oriented split arrays, index buffers, textures or renderer allocations, so it is a targeted saving rather than a guarantee that the private assembly will fit.

This lifetime change is in the shared `AssImpModelLoader`/viewer mesh-builder path. It therefore applies to JT, 3DXML and formats imported through Assimp rather than adding a JT-only allocation mode.

Hierarchy creation, animation setup, scene-index adjustment and success detection now use lightweight worker summaries and the actual count of viewer meshes attached during the import. This preserves those paths after progressive geometry has been released. OCC face remapping still keeps a temporary pre-optimization geometry copy for meshes carrying that metadata; other imported formats take the transfer path. The decoded `aiScene` remains alive until scene-graph creation and deep-copy/merge complete, so this is a staged geometry-lifetime improvement rather than a streaming scene representation.

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

Also smoke-check one small model in progressive mode, one in non-progressive mode and cancellation of a native CAD import. Include at least one textured glTF/GLB or OBJ model and one native JT or 3DXML model so the shared mesh-builder transfer path is covered. For the lazy rest-pose change, also check one morph-target model, one skinned model and clone/fragment behavior if those actions are available. Acceptance requires unchanged geometry, materials, visibility, hierarchy and animation behavior, plus atomic rollback on native failure/cancel. Then retry the private assembly with diagnostics enabled and retain the trace through viewer-mesh creation, scene graph, scene copy and scene merge. This document does not infer those results.

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
