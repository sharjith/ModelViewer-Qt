# ModelViewer tutorial

The tutorial is a set of plain HTML pages that ModelViewer shows in **Help > Tutorial** (an integrated dialog, or the web browser, depending on the user's choice). The same files can be opened directly in any browser: start at `index.html`.

## Contents

| Item | What it is |
|---|---|
| `index.html` | The home page with a card for every lesson |
| `lesson01.html` ... `lesson29.html` | The lessons, in English |
| `de/`, `es/`, `fr/`, `it/` | The same pages translated (index and all 29 lessons) |
| `common-styles.css` | The stylesheet shared by every page (the translated pages use `../common-styles.css`) |
| `screenshots/` | The pictures. All languages share one set, taken in the English UI |
| `screenshots/SHOT_LIST_19-29.md` | The pictures still to capture for lessons 19-29, with what each must show |

Lessons 1-18 are the core tutorial (interface, navigation, selection, views, materials, lighting, visibility, advanced features, performance, exploded views, morph targets, the transform gizmo, edges). Lessons 19-29 cover what 2026.11 added: simulation results (19-22), 3D data plots (23-25), mesh tools (26), measurement and annotation (27), analysis, selection and scenes (28), and ray tracing (29).

## How the dialog picks a page

`TutorialDialog` takes the page of the UI language from `data/tutorials/<language>/` when it exists and falls back to the English page at the top otherwise, so a lesson that is not translated yet still opens. The links inside a page are relative (`lesson05.html`, `index.html`); the dialog catches them and applies the same rule.

## Adding or changing a lesson

1. Edit the English page (or add `lessonNN.html`; copy the sidebar, progress bar and footer of a neighbour and add the lesson to the sidebar of **every** page, in all languages, and to `index.html`).
2. Make the same change in the four translations under `de/`, `es/`, `fr/`, `it/`.
3. Add the lesson's title to `TutorialDialog.cpp` (the list and `TOTAL_LESSONS`) and translate it in the `TutorialDialog` context of the `.ts` files.

Use the names of the controls exactly as the UI shows them in that language, in bold, and arrows (`File → Open...`) for menu paths.

## Screenshots

* Every picture is referenced as `screenshots/tutorial_<lesson>_<name>.png`. In lessons 19-29 the `alt` text of the image **is** that file name; in lessons 1-18 the `alt` text describes the picture (the expected file name is in the placeholder).
* While a picture is missing, the page shows a dashed placeholder with the file name and size. Its tooltip (the `title` attribute) says what to capture and which sample file to open.
* `screenshots/SHOT_LIST_19-29.md` lists them all as a checklist.
* Capture in the English UI with the default theme. A width of about 700 px is right for most; the page scales a picture down to the width of the text column.
* Animated gestures (lessons 2 and 3) are `.gif` files.

## Sample files used by the lessons

Lessons 19-25 use `sample-models/Simulation` and `sample-models/Plot3D`; both folders have a `README.txt` that describes every file. Lessons 26-29 use the CAD and mesh samples in `sample-models` (for example `RepairMeshTest.obj`, `OpenCylinder.obj`, `TorusTestCoarse.obj`, `bottle.step`, `MBB Gehause Rohteil.step`, the Forklift and the Futuristic Transport Shuttle).

## Installing

The `data` folder is installed as a whole, so the language folders and the screenshots go with it. For a manual copy, place this folder at `<ModelViewer data directory>/data/tutorials/`.
