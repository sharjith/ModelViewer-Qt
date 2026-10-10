# Simulation Results and 3D Plotting - user guide

A long-form guide (about 56 pages) to the 2026.11 features that go beyond models: reading simulation results and plotting your own data in 3D.
The **source** is plain HTML in `parts/` plus `guide.css`; the **PDF** is built from it.

| File | What it is |
|---|---|
| `parts/*.html` | The chapters, joined in file-name order (00 front matter, 10-15 part I, 20-23 part II, 30 part III, 40 appendices) |
| `guide.css` | The print stylesheet (A4, cover, callouts, tables, contents) |
| `build_guide.py` | Numbers the chapters, builds the contents, drops figures whose picture is missing, prints the PDF |
| `ModelViewer_Simulation_and_3D_Plotting_Guide.pdf` | The built guide |
| `guide.html` | Intermediate file written by the build (not tracked) |

## Building the PDF

```
python build_guide.py          # guide.html and the PDF
python build_guide.py --html   # only guide.html, to check the layout in a browser
```

Needs Python 3 and Microsoft Edge or Google Chrome (used headless; nothing is installed or changed). If PyMuPDF is installed (`pip install pymupdf`) the build reads the
chapter pages from the PDF and prints a second time, so the contents carry page numbers; without it the contents list the chapters only.

## Pictures

Figures are written as `<figure data-img="tutorial_19_first_result.png"><figcaption>...</figcaption></figure>`. The picture is taken from
`../../data/tutorials/screenshots/`, the same files the tutorial lessons use (see `SHOT_LIST_19-29.md` there), so one set of screenshots serves both. A figure whose
picture does not exist yet is left out of the build; capture the pictures and run the build again. The diagrams are inline SVG.

## Writing

* Chapters and sections are numbered by the build; refer to a chapter by its number in the text (the numbers in the text must be kept in step when chapters move).
* Callouts: `<div class="callout">` (note), `callout tip`, `callout warn`. Controls in `<span class="ui">`, menu paths in `<span class="path">`.
* Facts come from the program: the panel texts, `sample-models/*/README.txt` and the design notes in `docs/`. Check a statement against the UI before adding it.
