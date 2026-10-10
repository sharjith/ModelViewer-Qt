# Simulation Results and 3D Plotting - user guide

A long-form guide (about 56 pages) to the 2026.11 features that go beyond models: reading simulation results and plotting your own data in 3D.
The **source** is plain HTML in `parts/` (English) and `parts-de/`, `parts-es/`, `parts-fr/`, `parts-it/` (translations) plus `guide.css`; the **PDFs** are built from it.

| File | What it is |
|---|---|
| `parts/*.html` | The English chapters, joined in file-name order (00 front matter, 10-15 part I, 20-23 part II, 30 part III, 40 appendices) |
| `parts-de/`, `parts-es/`, `parts-fr/`, `parts-it/` | The same chapters translated (identical markup, file names and structure) |
| `guide.css` | The print stylesheet (A4, cover, callouts, tables, contents) |
| `build_guide.py` | Numbers the chapters, builds the contents, drops figures whose picture is missing, prints the PDF |
| `ModelViewer_Simulation_and_3D_Plotting_Guide.pdf`, `..._de.pdf`, `..._es.pdf`, `..._fr.pdf`, `..._it.pdf` | The built guides |
| `guide.html`, `guide_de.html` ... | Intermediate files written by the build (not tracked) |

## Building the PDF

```
python build_guide.py                 # the English guide: guide.html and the PDF
python build_guide.py --lang de       # one translation (de, es, fr or it)
python build_guide.py --lang all      # all five editions
python build_guide.py --html          # only the HTML, to check the layout in a browser
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

## Translations

The translations are kept in step with the English chapters block by block: every paragraph, list item, table cell, heading, caption and diagram label has the same position in
each `parts-xx/` folder, so a changed English paragraph is changed in the four translations in the same place. The UI names in `<span class="ui">` and `<span class="path">`
are the program's own translated strings (look them up in `translations/modelviewer_xx.ts`), not free translations. Code, file names and numbers stay as they are. The build
shrinks a diagram label that is too long for its box in a language, so text never runs out of a box; check the diagrams after changing a label.
