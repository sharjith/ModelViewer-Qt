# Closing with several unsaved documents: one review dialog

Status: designed and decided (2026-11-14); implemented the same day, **not built and not tested** (the owner builds and tests;
see section 11). The code observations in sections 2 and 3 are from reading the source on branch `dev`.

## 1. Problem

With several documents open and more than one modified, exiting asks one "save changes?" question per document, in turn, each
with its own Yes / No / Cancel. The owner wants one dialog: Save All, Don't Save (discard all), Cancel, and a way to choose
which documents to save from a list. The same dialog logic should serve the other close paths where it fits.

## 2. Current behaviour (observations from the code)

### 2.1 How a document knows it is modified

`ModelViewer::_documentModified` (`src/App/ModelViewer.cpp`, `setDocumentModified()`) is recomputed as
`_nonUndoDocumentDirty || _viewerStateDirty || hasUnsavedUndoDocumentChanges()`:

- `hasUnsavedUndoDocumentChanges()` compares the undo stack index with `_savedUndoIndex` and counts only commands whose
  `affectsDocument()` is true (macros are walked recursively).
- `markNonUndoDocumentModified()` sets `_nonUndoDocumentDirty` for persisted-but-not-undoable edits (measurement and
  annotation visibility, label orientation, environment panel, a deleted file on disk, ...).
- `_viewerStateDirty` comes from `checkViewerStateDirty()`, a timer-driven comparison of the saved view state (display mode,
  shading, environment, lights, ...) with a baseline. Camera, pan, zoom and projection are deliberately excluded from this
  comparison, so a camera-only change never raises a close prompt (it only matters for an explicit Save, via
  `hasUnsavedViewState()`).
- The title shows `Name*` (`refreshDocumentTitle()`).

### 2.2 Where the close questions are asked

| Path | Code | Behaviour today |
|---|---|---|
| Exit menu | `MainWindow::on_actionExit_triggered` -> `canExit()` | see below |
| Main window X, taskbar "Close all windows" | `MainWindow::closeEvent` (raises the window, then `canExit()`) | same |
| Close (Ctrl+W, Window menu) | `MainWindow::closeSubWindow` -> `ModelViewer::close()` | per-document prompt |
| Tab close button (MDI is in tabbed mode, `setTabsClosable(true)`) | QMdiArea closes the sub-window -> `ModelViewer::closeEvent` | per-document prompt |
| Close All | `MainWindow::closeAllSubWindows`: loops `viewer->close()` over a snapshot | one prompt per modified document; **a Cancel on one document does not stop the loop**, the next document is still asked and closed |
| Windows logoff / shutdown | no `commitDataRequest` handler in `src/`; Qt's default session handling closes windows (hypothesis, not verified in Qt source) | reaches `MainWindow::closeEvent` |

`MainWindow::canExit()` (`MainWindow.cpp`) loops over `_viewers` and **calls `child->closeEvent(&event)` directly** on each
document, stopping at the first one that ignores the event. Only when all accept does it show the optional "Are you sure you
want to exit?" (`checkConfirmExit` setting).

`ModelViewer::closeEvent` (`ModelViewer.cpp`) does, in order:

1. Refuses to close (and asks the analysis worker to cancel) while a Surface Analysis or Mass Properties computation runs.
2. If the document has unsaved **materials**: its own Yes / No / Cancel question ("You have %1 unsaved material(s)..."). Yes
   saves them to the user library (`saveMaterialsBeforeClose()`, which pops an information box on success and a warning on
   failure, plus a "Close anyway?" question if some failed).
3. `checkViewerStateDirty(true)` (forces the view-state comparison), then, if `_documentModified`, a Yes / No / Cancel question
   "The document has unsaved changes. Do you want to save before closing?" with Yes as default. Yes calls `save()`; a `false`
   return (cancelled or failed) ignores the close.
4. On accept: `cleanupUnsavedMaterialsFromLibrary()` removes this document's unsaved materials from the shared library.

Consequences for a multi-document exit:

- Up to two questions per document, plus up to two follow-up boxes, plus the final confirmation.
- Documents are visited in `_viewers` order, not necessarily the visible tab order, and the question does not say which
  document it is about (the text has no name; the document is usually not the active tab).
- Because `canExit()` calls `closeEvent()` directly, step 4 runs for each accepted document **before** the later documents and
  the "Are you sure" question have answered. If the user cancels at document 3 (or at "Are you sure"), documents 1 and 2 stay
  open but have already lost their unsaved materials from the shared library. This is an existing defect that the redesign
  removes (section 6.2).
- A document saved earlier in the loop stays saved when a later one cancels (correct and wanted, but the user is not told).

### 2.3 Save, Save As, never-saved documents

- `save()` (`ModelViewer.cpp`): if `_currentFile` has no `.mvf` extension (or is empty) it calls `saveAs()`. Documents opened by
  importing a CAD or mesh file get `_currentFile` set to the **imported** path (`MainWindow::loadFile` -> `setCurrentFile`), so
  they are "never saved as MVF" even though they have a name, and Save asks for a new `.mvf` name (the dialog proposes the
  import file name). A new document is titled `Session N` and has an empty `_currentFile`.
- `saveAs()` opens a native file dialog with the current document as parent; empty result returns `false`.
- `writeDocument(fileName, force)`:
  - "Nothing to save" shortcut when not modified, no unsaved view state and the file exists.
  - **File changed on disk** (`diskState() == Changed`, tracked by size and timestamp from the last read or write): a warning
    box with Overwrite / Save As... / Cancel, Cancel as default. Save As... re-enters `saveAs()`.
  - A file **deleted on disk** is not an error: `checkFileOnDisk()` marks the document modified and Save writes it again.
  - `promptSimulationSaveOptions()`: once per session, a modal choice of what to store, for documents with simulation
    results. Cancel returns `false`.
  - `saveToFile()` failure shows `QMessageBox::critical("Failed to save file: %1")` and returns `false`.
  - On success: `_currentFile`, tracked file, `_savedUndoIndex`, baselines and the modified flag are reset.
- Every one of these nested dialogs uses the document widget as parent. In `canExit()` the document being asked may be a
  background tab (the application does not activate it first).

### 2.4 What a failed or cancelled save does to the rest of the close

Today, any `false` from `save()` (cancelled file dialog, cancelled overwrite or simulation-options prompt, write failure)
makes `closeEvent` ignore the event, `canExit()` returns `false`, and the whole exit is abandoned. Earlier documents that were
already saved stay saved. Later documents are never visited. This conservative behaviour is right and is kept; the redesign
only has to make it visible and keep the failed document identifiable.

## 3. Research: conventions in comparable applications

Verification levels: **P** = read in a primary source during this research; **S** = from a search-result summary only;
**U** = not verified (searched or fetched, nothing usable).

### 3.1 Findings

| # | Finding | Level | Source |
|---|---|---|---|
| 1 | Qt Creator shows a "Save Changes" dialog listing the modified files (name and directory columns) in a multi-select tree with **all rows selected when it opens**; the primary button text tracks the selection: "Save All" when everything is selected, "Save Selected" for a partial selection, disabled when nothing is selected. Other buttons: "Do Not Save" (selects nothing and accepts), "Cancel", and an optional "Diff ... & Cancel". Save is the default button. A red warning line "Some files were modified externally. Saving them will discard the external changes." appears when a file changed on disk. Never-saved documents are listed under their fallback file name with an empty directory. | P | [saveitemsdialog.cpp (qt-creator master)](https://github.com/qt-creator/qt-creator/blob/master/src/plugins/coreplugin/dialogs/saveitemsdialog.cpp) |
| 2 | Visual Studio's classic "Save Changes" dialog lists items under "Save changes to the following items?"; items can be selected and cleared; selected items are saved to their existing location; an item never saved before opens Save File As. The page names only Yes (not the rest of the button set). | P (archived VS 2008) | [Save Changes Dialog Box](https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2008/fwxk5dad(v=vs.90)) |
| 3 | The VS solution-close prompt uses Save / Discard / Cancel with Save as the default (API documentation). | S | [IVsSolution3.CheckForAndSaveDeferredSaveSolution](https://msdn.microsoft.com/en-US/library/bb141330(v=vs.120)) |
| 4 | Windows confirmation guidance: for save prompts use specific responses ("Save" and "Don't save"), never OK/Cancel; prompt only for significant changes the user made; the page's own example is a close-with-unsaved-changes prompt. Labels start with a verb, sentence-style capitalisation, no access key on Cancel. Commit buttons are right-aligned in one row. | P | [Confirmations](https://learn.microsoft.com/en-us/windows/win32/uxguide/mess-confirm), [Dialog boxes](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-dialog-box). Both pages note they were written for Windows 7 and not updated. |
| 5 | Esc closes a dialog with the effect of Cancel; the title-bar close button must have the same effect as Cancel/Close, never OK. Every dialog needs a default button; for questions it should be the safest response, and a destructive response must not be the default unless easily undone. | P for Esc and title-bar close (verbatim); the default-button rule and the "Save / Don't Save / Cancel" pattern with a separate negative button come from the unread part of the page, via a model summary | [Dialog boxes](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-dialog-box) |
| 6 | Qt: `QDialogButtonBox` offers `Save`, `SaveAll`, `Discard` (`DestructiveRole`; shown as "Discard" or "Don't Save" depending on platform), `Cancel` (`RejectRole`); button order follows the platform `ButtonLayout`. `QMessageBox` picks the escape button by rule (a Cancel button, else a single Reject/No-role button). | P (QDialogButtonBox, QMessageBox rules verbatim; tail of QMessageBox from a summary) | [QDialogButtonBox](https://doc.qt.io/qt-6/qdialogbuttonbox.html), [QMessageBox](https://doc.qt.io/qt-6/qmessagebox.html) |
| 7 | Qt session management: on Windows, connecting `QGuiApplication::commitDataRequest` is "all you can and must provide"; interaction is allowed only if `QSessionManager::allowsInteraction()`; if the user cancels call `cancel()`; the handler must not exit the app. Windows can terminate the process after Qt has closed the windows at logoff. | P | [QGuiApplication::commitDataRequest](https://doc.qt.io/qt-6/qguiapplication.html#commitDataRequest), [QSessionManager](https://doc.qt.io/qt-6/qsessionmanager.html), [Session Management](https://doc.qt.io/qt-6/session.html), [QApplication::exec note](https://doc.qt.io/qt-6/qapplication.html) |
| 8 | VS Code does not ask by default: "hot exit" remembers unsaved changes on exit (`files.hotExit`), and `files.autoSave` is an option. Its Save All once refused to save anything while an untitled file needed Save As; the fix was to save what can be saved immediately. | P (hot exit section and the issue) | [Basic editing](https://code.visualstudio.com/docs/editing/codebasics), [vscode#7531](https://github.com/microsoft/vscode/issues/7531) |
| 9 | JetBrains IDEs save automatically on closing a file or project and on quitting; manual Save All exists. | P | [Saving and reverting changes](https://www.jetbrains.com/help/idea/saving-and-reverting-changes.html) |
| 10 | Qt Creator users reported several separate prompts when closing several modified files at once with the keyboard (the situation this note removes). | S | [QTCREATORBUG-1732 and related search hits](https://bugreports.qt-project.org/browse/QTCREATORBUG-1732) |
| 11 | Apple HIG (current): do not make Cancel the default button of an alert; a destructive style marks the button that performs the destructive action; labels should be specific verbs. The older Mac guideline for save alerts is Save (default, right), Cancel, and Don't Save separated to the left. | S (the HIG page body would not render for the fetch tool; summary of the page and of the c. 1999 guideline) | [HIG: Alerts](https://developer.apple.com/design/human-interface-guidelines/alerts) |
| 12 | Blender asks to confirm or save on quit; no per-file list was confirmed. | S | [Blender manual: Opening & Saving](https://docs.blender.org/manual/en/latest/files/blend/open_save.html) |
| 13 | Photoshop ("Review Changes"/Save All), SolidWorks, Fusion, Word, and the exact current Visual Studio exit prompt. | **U**: fetches returned 403, a landing page, or nothing relevant; no claim is made about them here. They remain a gap and the owner may test them directly. |  |

### 3.2 What the evidence supports

- The checklist-with-everything-selected pattern, with a button whose text follows the selection, is documented in a mature Qt
  application of the same toolkit (finding 1) and in classic Visual Studio (finding 2). It is a proven pattern, and Qt
  Creator's source can be consulted directly (it is open source) for detail.
- Windows guidance asks for specific verbs ("Save", "Don't save") and Cancel/Esc/title-bar close all meaning "abandon the
  task" (findings 4, 5).
- Save-as-default is consistent with Windows, Qt Creator and the Qt example; the Apple note about not defaulting to Cancel
  agrees. A discard button must not be the default.
- No source found describes a save-failure policy for a batch; that part of the design (section 4.4) is reasoned from the
  current code behaviour and the VS Code Save All lesson (save what can be saved, do not let one problem block the rest).

## 4. Recommended dialog

### 4.1 When it appears

- Exit (menu, window X, taskbar close, logoff) and **Close All**: if two or more documents need review, show the review
  dialog once. Documents with no unsaved content never appear in it.
- Exactly one document needs review: keep a plain message box with the same three buttons, now naming the document (and
  mentioning unsaved materials if any). One row in a list is just noise.
- Closing a single document (Ctrl+W, tab X, Close): the same single-document message box (it must name the document).
- Nothing to review: no dialog, as today.

### 4.2 Layout (text description)

Title: **Save changes** (title-style, no punctuation, matching the Windows guidance). No title-bar icon. Modal, parented to the
main window, remembers its size (project convention for dialogs).

1. Main instruction (one sentence, wraps): *"Save changes to these 3 documents before closing?"* When the exit is part of the
   flow the text may be *"before exiting?"*. The count updates nothing; it is the number of modified documents.
2. A checkable list, one row per modified document, **all rows checked on open**, in the order of the tabs. Columns:
   Document (check box, name), Location (folder, elided in the middle, full path in the tooltip), Status.
3. A warning line (only when needed, with a text prefix, not only colour): *"Some files were changed on disk. Saving asks
   before overwriting them."*
4. A consequence line that tracks the selection (the one place where "Don't Save" semantics are spelled out): *"2 documents
   will be saved. 1 will be closed without saving."*
5. Button row, right-aligned in Windows order: **Save All** (default), **Don't Save**, **Cancel**.

Button behaviour:

| Selection state | Primary button text | Effect |
|---|---|---|
| all rows checked | Save All | save every row, then close/exit |
| some rows checked | Save Selected | save checked rows; unchecked rows are closed without saving (stated in the consequence line) |
| none checked | Save Selected, disabled | the user must choose Don't Save or Cancel |
| (any) Don't Save | Discard changes of all rows | close/exit without saving anything; no second confirmation (it is the button the user read the label of, and the list is in front of them) |
| (any) Cancel, Esc, title-bar X | nothing closes | nothing is saved by this click; documents saved earlier in the same flow remain saved |

The dialog never saves anything itself; it returns the user's decision and the list of documents to save, and the caller
performs the saves (4.4).

### 4.3 Mock-up

```
+--------------------------------------------------------------+
|  Save changes                                            [x] |
|                                                              |
|  Save changes to these 3 documents before exiting?           |
|                                                              |
|  +--------------------------------------------------------+  |
|  | [x] Document        Location          Status           |  |
|  | [x] bracket.mvf     ...\Projects      Modified         |  |
|  | [x] Session 2       -                 Never saved      |  |
|  | [x] engine.step     ...\Imports       Never saved      |  |
|  |                                       as .mvf          |  |
|  +--------------------------------------------------------+  |
|  ! 1 file was changed on disk. Saving asks before            |
|    overwriting it.                                           |
|  3 documents will be saved.                                  |
|                                                              |
|              [ Save All ]  [ Don't Save ]  [ Cancel ]        |
+--------------------------------------------------------------+
```

Status values (always text; an icon may accompany but never replaces it):

| Document state | Status text | Notes |
|---|---|---|
| `.mvf` file, modified | "Modified" | saved in place |
| empty `_currentFile` (`Session N`) | "Never saved - asks for a file name" | Save As dialog on its turn |
| `_currentFile` is an imported file (not `.mvf`) | "Not yet saved as .mvf - asks for a file name" | the imported file itself is never overwritten (current rule) |
| `diskState() == Changed` | "Changed on disk - asks before overwriting" | warning line shown; the existing Overwrite / Save As... / Cancel box stays the safeguard |
| `diskState() == Missing` | "File deleted from disk - will be written again" | matches `checkFileOnDisk()` semantics |
| unsaved materials present | appended: "+ 2 unsaved materials" | see 4.5 |

### 4.4 Execution after the user chooses Save

Sequential, in list order, one document at a time:

1. Activate the document (so its tab is visible, and Save As / overwrite / simulation-options prompts that parent to the
   document are on screen and identify it). Show "Saving 2 of 3: bracket.mvf" in the status bar (or a small progress label).
2. Refresh its modified state (`checkViewerStateDirty(true)`), then `save()`.
3. Outcomes:
   - **Success**: continue.
   - **User cancelled something inside the save** (Save As file dialog, overwrite box, simulation-options dialog): treat as
     **Cancel for the whole close**: stop, do not close anything, leave the application running. Documents saved so far
     stay saved. This equals today's behaviour (section 2.4).
   - **Write failure** (`saveToFile` false): the existing error box appears for that document; then stop the same way, keep
     the failed document active with its `*` mark, and do not exit. Recommended refinement (step 6 of the plan): offer
     "Skip this document", "Retry" and "Cancel" on that error so one unwritable file does not force the user to resolve it
     before saving the others; if skipped, finish the others and then leave the application open.
4. When every selected document has saved, close/exit: documents that were unchecked or discarded are closed without saving.

Order matters for safety: **nothing is closed or discarded until all chosen saves have succeeded.** This is the main
behaviour difference from the per-document loop, and it removes the partial-close state described in 2.2.

### 4.5 Unsaved materials

The per-document material question ("You have %1 unsaved material(s)...") is a second, different kind of data (user library
entries, not the document). Recommendation: do not add a second dialog per document. Two options, owner to choose (open
question 3): (a) show them only as a count in the Status column and, when the row is saved, also save the materials silently
(this is what "Save All" already means in the existing comment in `saveMaterialsBeforeClose()`), reporting one summary line
afterwards; or (b) keep today's per-document material question but only after the review dialog and only for rows that are
being closed. Option (a) is recommended.

### 4.6 Keyboard and accessibility

- Enter activates Save All / Save Selected (default button); Esc and the title-bar X mean Cancel (matches the Windows
  guidance and `QMessageBox` escape rules; set the escape button explicitly).
- Initial focus on the list's first row; Space toggles a row; Tab order: list, then buttons in visual order.
- Access keys: Save &All is not assigned on Cancel (Windows guidance); `Do&n't Save` follows platform convention for
  "Don't Save" (N); verify there are no clashes in de/es/fr/it when translating.
- Every status is text; colour (red/amber) only reinforces. The consequence line and warning line are real labels (screen
  readers read them); give the list and rows accessible names ("bracket.mvf, Modified, selected for saving").
- Support high-contrast and the app's light/dark themes through the standard palette (no hard-coded colours).
- Pointer: click on a row toggles nothing except through the check box; double-click does nothing (do not switch tab while
  the dialog is modal).

## 5. Alternatives considered

| Alternative | Why not |
|---|---|
| Keep one prompt per document, only add the document name | Cheapest, but the owner's complaint (N sequential questions, no overview, no Save All) stays. Still needed for the single-document case. |
| Per-document prompt with a "Yes to All / No to All" pair (Qt `YesToAll`, `NoToAll`) | Familiar in file managers, but the user still answers the first question blind, with no list to see what "all" is, and cannot pick a subset. |
| Two buttons only: Save All / Don't Save, no list | Fastest, but gives no choice which to keep, and is easy to misread as destructive when some documents are old. |
| Multi-select (Qt Creator's tree) instead of check boxes | Qt Creator's own approach and compact, but selection is less discoverable and less accessible than check boxes, and conflicts with keyboard "Space toggles". The dynamic "Save All / Save Selected" label is borrowed from it. |
| Auto-save / hot-exit (VS Code style) and no prompt | The better long-term answer for data safety, but it changes what an MVF session file means (MVF is a session file, not a document format), needs a recovery store and a "restore" flow, and is far larger. Recorded as a possible later feature, not a replacement. |
| Checklist with all rows unchecked by default | Safer against accidental saves but makes the common case (save everything) two clicks and contradicts the conventions in 3.1 (rows start selected). |
| Extra "Are you sure?" after Don't Save | The Windows guidance asks for confirmation only for significant data loss, and the dialog already lists exactly what is lost. A second prompt reintroduces the dialog stacking the owner wants gone. |
| Keep the generic "Are you sure you want to exit?" after the review | Redundant after a review dialog the user just answered. See 6.3. |

## 6. Fit with the existing code

All names below exist in the current code except those marked *new*.

### 6.1 Where it is raised

In `MainWindow::canExit()` (and in `closeAllSubWindows()`), replacing the loop that calls `child->closeEvent()` directly:

1. Collect documents to review (*new* `MainWindow::collectUnsavedDocuments()`): for each viewer in tab order, call a *new*
   `ModelViewer::refreshModifiedForClose()` (wraps `checkViewerStateDirty(true)` and returns `_documentModified`), skipping
   documents whose analysis computation is in flight (they keep today's "cancel and try again" refusal, which should stay a
   per-document early-out: show it once for the first such document and abort the close).
2. 0 documents: no dialog. 1 document: the single-document box. 2+: the review dialog (*new* `UnsavedChangesDialog`, a
   `QDialog` in `src/App/`, following the dialog conventions of the project: `.ui` form if the owner wants, remembered
   geometry, `tr()` strings).
3. Run the sequential save (4.4) in a *new* `MainWindow::saveDocumentsForClose(const QList<ModelViewer*>&)` that returns a
   tri-state (`Done`, `Cancelled`, `Failed`).
4. Only if `Done`: continue to the rest of `canExit()`, then commit. The commit step (*new*
   `ModelViewer::finalizeClose()`, containing `cleanupUnsavedMaterialsFromLibrary()`) runs for every document **after** the
   decision to exit is final, fixing the defect in 2.2. Documents are not closed one by one on exit (the process exits), so
   nothing re-prompts; for Close All, set a *new* `_closeApproved` flag on each document so its real `closeEvent` skips the
   prompt, then call `close()`.

`ModelViewer::closeEvent` keeps its current path for the single-document close (tab X, Ctrl+W), but uses the same helper for
the message box so wording is shared, and skips the prompt when `_closeApproved` is set.

### 6.2 Data needed from each document

A small value type, *new* `UnsavedDocumentInfo`, filled by `ModelViewer` (public const getters; most data already exists):

| Field | Source |
|---|---|
| document pointer | the `ModelViewer*` (not owned; use `QPointer`) |
| display name | `documentBaseTitle()` |
| folder / full path | `currentFile()` (empty for untitled) |
| kind: `Mvf`, `Untitled`, `ImportedNotMvf` | extension of `currentFile()` as in `save()` |
| disk state | `diskState()` (currently private; expose read-only) |
| unsaved material count | `predefinedMaterialsPanel->getUnsavedMaterialKeys().size()` |

### 6.3 Cancel and failure handling

- Cancel in the dialog: return `false` from `canExit()` / skip closing; nothing is touched, no cleanup runs.
- Cancel or failure during a save: stop immediately (4.4); `canExit()` returns `false`; activate the failed document. No
  cleanup runs for any document.
- The "Are you sure you want to exit?" confirmation (`checkConfirmExit`) is skipped when the review dialog was shown and
  answered Save or Don't Save (the user has just made an explicit exit decision). It is kept when no dialog was needed.
  Owner to confirm (open question 4).
- `closeAllSubWindows()` must stop at the first Cancel instead of continuing to the next document (a current inconsistency).
- Windows logoff: connect `commitDataRequest` (Qt Windows guidance, finding 7). If `allowsInteraction()` is true, run the same
  review flow and call `cancel()` on Cancel; if not, do nothing destructive. Recorded as a separate, later step because it
  needs testing under a real logoff, which the owner would run.

### 6.4 Other close paths

| Path | Handling |
|---|---|
| Window X, taskbar close | goes through `MainWindow::closeEvent` -> `canExit()`; the existing raise/activate code stays |
| Exit menu | same |
| Close All | review dialog scoped to all documents; stop on Cancel |
| Ctrl+W, tab X, Close | single document box (names the document) |
| Failed load (`child->close()` in `loadFile`) | unaffected (document is not modified) |

## 7. Strings needing translation (de, es, fr, it in `translations/*.ts`)

New (context: new class `UnsavedChangesDialog`, plus `MainWindow` for the single-document box and status messages):

- `Save changes`
- `Save changes to these %n documents before closing?` (plural form, `%n`)
- `Save changes to these %n documents before exiting?` (plural form, `%n`)
- `Save changes to "%1" before closing?` and `Save changes to "%1" before exiting?`
- `Document`, `Location`, `Status`
- `Modified`
- `Never saved - asks for a file name`
- `Not yet saved as .mvf - asks for a file name`
- `Changed on disk - asks before overwriting`
- `File deleted from disk - will be written again`
- `+ %n unsaved material(s)` (plural form)
- `Some files were changed on disk. Saving asks before overwriting them.`
- `%n document(s) will be saved.` / `%n document(s) will be closed without saving.` (plural forms)
- `Save All`, `Save Selected`, `Don't Save`, `Cancel`
- `Saving %1 of %2: %3`
- `Could not save "%1". The application stays open.`
- (refinement) `Skip this document`, `Retry`

Reused from the current code (already translated; keep source text identical where possible): `Unsaved Changes`, `The document has
unsaved changes. Do you want to save before closing?` (replaced by the named variants above, so its translations become
unused), `Unsaved Materials`, `You have %1 unsaved material(s). Do you want to save them?`, `Save Failed`, `File changed on
disk`, `Overwrite`, `Save As...`, `Failed to save file: %1`, `Confirm Exit`.

Translation notes: check access-key conflicts in each language; "Don't Save" should use the platform wording used by the
four translations' existing `QDialogButtonBox` buttons if the owner uses standard buttons.

## 8. Implementation plan (small steps; the owner builds and tests after each)

1. **Refactor, no behaviour change**: add `ModelViewer::refreshModifiedForClose()`, `UnsavedDocumentInfo` and its getters,
   expose `diskState()`; add `finalizeClose()` and move `cleanupUnsavedMaterialsFromLibrary()` out of the accept path used by
   `canExit()` (fixes 2.2). Test: exit with 3 documents, cancel at the last one, check the first two still show their
   unsaved materials.
2. **Single-document box names the document** (wording only). Test: Ctrl+W on a background-less tab, tab X.
3. **`UnsavedChangesDialog`** (list, check boxes, dynamic primary button, consequence line, status texts, keyboard), exercised
   from a temporary or hidden test entry point if the owner wants to look at it before wiring. Test: look and feel, keyboard,
   translations of long names, high DPI, dark theme.
4. **Wire into `canExit()`** with sequential save and the Cancel/failure rules; skip the redundant "Are you sure" after a
   review. Test matrix: untitled + imported + `.mvf` + changed-on-disk + deleted-on-disk; cancel Save As; read-only target
   (write failure); simulation-results document (options prompt appears for that row only).
5. **Close All**: use the dialog, and stop at the first Cancel.
6. **Failure refinement**: Skip / Retry / Cancel on a write failure.
7. **Logoff**: `commitDataRequest` handler (Windows test by the owner).
8. **Translations**: add the strings of section 7 to the four `.ts` files.

## 9. Owner decisions (2026-11-14)

The owner accepted the recommendation on all eight questions below. The owner then asked for everything to be implemented (section 11).

1. Single modified document: three-button message box naming the document.
2. Unchecked rows with Save Selected: closed without saving.
3. Unsaved materials: saved silently with Save All, with one summary line.
4. The "Are you sure you want to exit?" confirmation is skipped after the review dialog.
5. A cancelled Save As cancels the whole exit.
6. A write failure stops at once in v1; Skip / Retry / Cancel stays as plan step 6.
7. A document changed on disk starts checked, with the warning line.
8. Hot exit / recovery: not wanted now; recorded as a possible later feature (no recommendation had been made beyond that).

## 9a. Original open questions

1. Single modified document: keep the three-button message box (recommended), or use the list dialog even for one row?
2. Unchecked rows with "Save Selected": closed without saving (recommended, spelled out in the consequence line), or kept open?
3. Unsaved materials (4.5): silent save with Save All (recommended), or keep today's separate question?
4. Skip the "Are you sure you want to exit?" confirmation after the review dialog (recommended)?
5. When a Save As file dialog is cancelled for one untitled document: cancel the whole exit (recommended, matches today), or
   skip just that document and continue?
6. On a write failure: stop at once (v1) or offer Skip/Retry/Cancel (step 6)?
7. Should a document whose file changed on disk start **unchecked** (safer) or checked with a warning (recommended, because
   Save already asks before overwriting)?
8. Longer term: is a recovery/hot-exit feature wanted? It would reduce the need for this dialog but changes the MVF session
   semantics.

## 10. Evidence limits

- Research level markers are in section 3.1. The current Visual Studio exit prompt, Photoshop, SolidWorks, Fusion and Word were
  not verified from primary sources; no claim is made about them.
- The Apple HIG page could not be rendered by the fetch tool; those statements rest on a search summary.
- The Windows behaviour of Qt's default session management at logoff was not checked in Qt source (hypothesis in 2.2).
- All code observations are from reading; nothing was built or run.

## 11. Implementation status (2026-11-14, untested)

Written but not compiled or run. New files: `include/App/UnsavedChangesDialog.h`, `src/App/UnsavedChangesDialog.cpp`. Changed:
`ModelViewer.h/.cpp` (`cancelAnalysisIfBusy`, `refreshModifiedForClose`, `unsavedMaterialCount`, `unsavedDocumentInfo`, `confirmClose`,
`saveMaterialsQuietly`, `finalizeClose`, `setCloseApproved`, `lastSaveFailed`; `closeEvent` now = busy check, `confirmClose()` unless approved,
`finalizeClose()`), `MainWindow.h/.cpp` (`reviewUnsavedDocuments`, `activateDocumentForClose`; `canExit` and `closeAllSubWindows` use them),
and the five `translations/modelviewer_*.ts` files (strings of section 7 added by hand, with de/es/fr/it translations; run `lupdate`
afterwards to refresh locations, and have a native speaker check the wording).

Differences from the plan:

- **Logoff handler (plan step 7) not added.** Qt's `QApplication` default session handling closes the top-level windows when
  interaction is allowed, which reaches `MainWindow::closeEvent` -> `canExit()` and so the new review; a second `commitDataRequest`
  handler would risk asking twice. This is from reading the Qt docs, not tested on a real logoff.
- A document that has only unsaved **materials** (not modified itself) is listed too, with the status "N unsaved material(s)".
- If saving the materials of a row fails (that call already shows its own warning), the whole close is cancelled.
- The single-document question always says "before closing" (also on exit); only the multi-document dialog distinguishes exit.
- Cancelling an analysis computation: the close stops, the document is activated and a status message says to close again.

Test matrix for the owner: exit with 0, 1, 2+ modified documents; untitled, imported (non-.mvf) and `.mvf` rows; a file changed and a
file deleted on disk; cancel the review; cancel a Save As; a read-only target (Retry / Skip / Cancel); a document with unsaved
materials; a simulation-results document (options prompt); Close All; tab X and Ctrl+W on one modified document; cancel at the last
document and check that earlier documents keep their unsaved materials (the old premature cleanup); Esc, Enter, Space and Tab in the
dialog; German/French/Italian/Spanish button access keys; dark theme and high DPI.
