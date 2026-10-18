#!/usr/bin/env python3
"""Builds the Simulation Results and 3D Plotting guide PDF from the HTML chapters in parts/.

    python build_guide.py                builds guide.html and the PDF (English)
    python build_guide.py --lang de     the German edition (also es, fr, it): reads parts-de/ and writes guide_de.html and the PDF with a _de suffix
    python build_guide.py --lang all    all five editions
    python build_guide.py --html        only the HTML (open it in a browser to check the layout)

The chapters are plain HTML fragments (parts/NN_*.html, joined in file-name order). The build
  * numbers the chapters (1, 2, ...), the sections (1.1, ...) and the appendices (A, B, ...), and writes the table of contents;
  * turns <figure data-img="name.png"> into a picture taken from ../../data/tutorials/screenshots/ (the same files the tutorial lessons
    use) and DROPS the figure while that picture does not exist yet, so the PDF never shows a broken image;
  * prints the page with headless Microsoft Edge or Google Chrome (nothing else is needed);
  * if PyMuPDF is installed (pip install pymupdf) it reads the page of every chapter from the PDF and prints a second time so that the
    contents carry page numbers. Without it the contents list the chapters without page numbers.
"""
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.normpath(os.path.join(HERE, '..', '..', 'data', 'tutorials', 'screenshots'))
PDF_BASE = 'ModelViewer_Simulation_and_3D_Plotting_Guide'
LANGS = ['en', 'de', 'es', 'fr', 'it']

# Text the build itself writes (the chapters carry everything else).
UI = {
    'en': {'contents': 'Contents', 'title': 'ModelViewer - Simulation Results and 3D Plotting Guide'},
    'de': {'contents': 'Inhalt', 'title': 'ModelViewer - Handbuch Simulationsergebnisse und 3D-Diagramme'},
    'es': {'contents': 'Contenido', 'title': 'ModelViewer - Guía de resultados de simulación y gráficos 3D'},
    'fr': {'contents': 'Sommaire', 'title': 'ModelViewer - Guide des résultats de simulation et des graphiques 3D'},
    'it': {'contents': 'Indice', 'title': 'ModelViewer - Guida ai risultati di simulazione e ai grafici 3D'},
}

LANG = 'en'
OUT_HTML = os.path.join(HERE, 'guide.html')
OUT_PDF = os.path.join(HERE, PDF_BASE + '.pdf')


def set_language(lang):
    """Selects the edition: the chapters folder and the output file names."""
    global LANG, OUT_HTML, OUT_PDF
    LANG = lang
    suffix = '' if lang == 'en' else '_' + lang
    OUT_HTML = os.path.join(HERE, 'guide%s.html' % suffix)
    OUT_PDF = os.path.join(HERE, '%s%s.pdf' % (PDF_BASE, suffix))

NL = chr(10)
BACKSLASH = chr(92)

BROWSERS = [
    r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
    r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
    r'C:\Program Files\Google\Chrome\Application\chrome.exe',
    r'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
    '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/microsoft-edge',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
]


def find_browser():
    for p in BROWSERS:
        if os.path.exists(p):
            return p
    for name in ('msedge', 'chrome', 'google-chrome', 'chromium'):
        p = shutil.which(name)
        if p:
            return p
    return None


def read_parts():
    parts_dir = os.path.join(HERE, 'parts' if LANG == 'en' else 'parts-' + LANG)
    names = sorted(n for n in os.listdir(parts_dir) if n.endswith('.html'))
    return ''.join(open(os.path.join(parts_dir, n), encoding='utf-8').read() + NL for n in names)


def resolve_figures(body):
    kept = dropped = 0

    def fig(m):
        nonlocal kept, dropped
        img = m.group(1)
        inner = m.group(2)
        path = os.path.join(SHOTS, img)
        if not os.path.exists(path):
            dropped += 1
            return ''
        kept += 1
        uri = 'file:///' + path.replace(BACKSLASH, '/')
        return '<figure><img src="%s" alt="%s">%s</figure>' % (uri, html.escape(img), inner)

    body = re.sub(r'<figure data-img="([^"]+)">(.*?)</figure>', fig, body, flags=re.S)
    return body, kept, dropped


def number_headings(body):
    """Numbers chapters/sections/appendices and returns (new body, toc entries)."""
    toc = []
    chapter = 0
    appendix = 0
    section = 0
    in_appendix = False
    in_front = False

    def repl(m):
        nonlocal chapter, appendix, section, in_appendix, in_front
        tag, attrs, text = m.group(1), m.group(2), m.group(3)
        plain = re.sub(r'<[^>]+>', '', text.replace('</small>', ' - ')).strip()
        slug = re.sub(r'[^a-z0-9]+', '-', plain.lower()).strip('-')
        if tag == 'h1' and 'class="part"' in attrs:
            in_front = False
            toc.append(('part', '', plain, slug))
            return '<h1 class="part" id="%s">%s</h1>' % (slug, text)
        if tag == 'h1' and 'front' in attrs:
            in_front = True
            toc.append(('front', '', plain, slug))
            return '<h1 class="chapter" id="%s" data-running="%s" style="break-before: auto; margin-top: 0;">%s</h1>' % (slug, html.escape(plain), text)
        if tag == 'h1' and 'chapter' in attrs:
            in_front = False
            if 'appendix' in attrs:
                appendix += 1
                in_appendix = True
                num = chr(ord('A') + appendix - 1)
            else:
                chapter += 1
                num = str(chapter)
            section = 0
            toc.append(('l1', num, plain, slug))
            return '<h1 class="chapter" id="%s" data-running="%s  %s">%s&nbsp;&nbsp;%s</h1>' % (slug, html.escape(num), html.escape(plain), num, text)
        if tag == 'h2' and in_front:
            return '<h2 id="%s">%s</h2>' % (slug, text)
        if tag == 'h2':
            section += 1
            num = '%s.%d' % ((chr(ord('A') + appendix - 1) if in_appendix else str(chapter)), section)
            toc.append(('l2', num, plain, slug))
            return '<h2 id="%s">%s&nbsp;&nbsp;%s</h2>' % (slug, num, text)
        return m.group(0)

    body = re.sub(r'<(h1|h2)([^>]*)>(.*?)</\1>', repl, body, flags=re.S)
    return body, toc


def build_toc(entries, pages=None):
    """pages: optional list of page numbers, one per entry, in order."""
    out = ['<section class="toc"><h1>%s</h1><ol>' % UI[LANG]['contents']]
    for i, (kind, num, text, slug) in enumerate(entries):
        label = html.escape(text)
        pg = ''
        if pages and i < len(pages):
            pg = '<span class="dots"></span><span class="pg">%d</span>' % pages[i]
        if kind == 'front':
            out.append('<li class="l1"><a href="#%s">%s%s</a></li>' % (slug, label, pg))
        elif kind == 'part':
            out.append('<li class="part"><a href="#%s">%s%s</a></li>' % (slug, label, pg))
        elif kind == 'l1':
            out.append('<li class="l1"><a href="#%s">%s&nbsp;&nbsp;%s%s</a></li>' % (slug, html.escape(num), label, pg))
        elif kind == 'l2':
            out.append('<li class="l2"><a href="#%s">%s&nbsp;&nbsp;%s%s</a></li>' % (slug, html.escape(num), label, pg))
    out.append('</ol></section>')
    return NL.join(out)


FIT_SCRIPT = '''<script>
// Translated labels are longer than the English ones: shrink an SVG label until it fits inside the box it sits in.
window.addEventListener('load', function () {
  document.querySelectorAll('svg').forEach(function (svg) {
    var rects = Array.prototype.slice.call(svg.querySelectorAll('rect'));
    svg.querySelectorAll('text').forEach(function (t) {
      var x = parseFloat(t.getAttribute('x')), y = parseFloat(t.getAttribute('y'));
      var best = null;
      rects.forEach(function (r) {
        var rx = parseFloat(r.getAttribute('x')), ry = parseFloat(r.getAttribute('y'));
        var rw = parseFloat(r.getAttribute('width')), rh = parseFloat(r.getAttribute('height'));
        if (x > rx && x < rx + rw && y > ry && y < ry + rh && (!best || rw * rh < best.w * best.h)) best = {w: rw, h: rh};
      });
      var avail = null, w = t.getBBox().width;
      if (best) {
        avail = best.w - 12;
      } else if (t.getAttribute('text-anchor') !== 'middle' && t.getAttribute('text-anchor') !== 'end') {
        // A label left of a bar: it may run up to the next box on its line.
        rects.forEach(function (r) {
          var rx = parseFloat(r.getAttribute('x')), ry = parseFloat(r.getAttribute('y')), rh = parseFloat(r.getAttribute('height'));
          if (rx > x && y > ry - 4 && y < ry + rh + 4 && (avail === null || rx - x - 8 < avail)) avail = rx - x - 8;
        });
      }
      if (avail === null) return;
      if (w > avail) {
        var fs = parseFloat(window.getComputedStyle(t).fontSize) || 12;
        t.setAttribute('font-size', Math.max(7.5, fs * avail / w).toFixed(2));
      }
    });
  });
});
</script>'''


def write_html(body_template, entries, pages=None):
    body = body_template.replace('<!--TOC-->', build_toc(entries, pages))
    doc = ('<!DOCTYPE html>' + NL + '<html lang="%s"><head><meta charset="utf-8">' % LANG +
           '<title>%s</title>' % html.escape(UI[LANG]['title']) +
           '<link rel="stylesheet" href="guide.css"></head><body>' + NL + body + NL + FIT_SCRIPT + NL + '</body></html>' + NL)
    with open(OUT_HTML, 'w', encoding='utf-8', newline=NL) as f:
        f.write(doc)


def print_pdf(browser):
    # A private profile makes the browser start its own process (otherwise a running Edge/Chrome takes the request over and exits at once).
    profile = tempfile.mkdtemp(prefix='mvguide_profile_')
    if os.path.exists(OUT_PDF):
        os.remove(OUT_PDF)
    cmd = [browser, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
           '--user-data-dir=' + profile, '--print-to-pdf=' + OUT_PDF, 'file:///' + OUT_HTML.replace(BACKSLASH, '/')]
    try:
        subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        # The browser can hand the work to a child process and return early: wait until the file exists and has stopped growing.
        last, stable = -1, 0
        for _ in range(240):
            size = os.path.getsize(OUT_PDF) if os.path.exists(OUT_PDF) else -1
            stable = stable + 1 if (size == last and size > 0) else 0
            last = size
            if stable >= 3:
                break
            time.sleep(0.5)
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    return os.path.exists(OUT_PDF)


def toc_pages(count):
    """The page every contents entry points to, read from the PDF's internal links (needs PyMuPDF; None without it)."""
    try:
        import pymupdf
    except ImportError:
        return None
    doc = pymupdf.open(OUT_PDF)
    links = []
    for pno in range(len(doc)):
        page = doc[pno]
        # The only internal links in the document are the contents entries.
        for lk in page.get_links():
            if lk.get('page', -1) >= 0:
                links.append((pno, lk['from'].y0, lk['page'] + 1))
    links.sort()
    pages = [p for _, _, p in links]
    doc.close()
    return pages if len(pages) == count else None


def build(lang, html_only, browser):
    set_language(lang)
    body = read_parts()
    body, kept, dropped = resolve_figures(body)
    body, entries = number_headings(body)
    write_html(body, entries)
    print('[%s] %s: %d chapters/sections listed, %d pictures included, %d not captured yet (their figures are left out)'
          % (lang, os.path.basename(OUT_HTML), len(entries), kept, dropped))
    if html_only:
        return 0
    if not print_pdf(browser):
        print('The browser did not write the PDF.')
        return 1
    pages = toc_pages(len(entries))
    if pages:
        # Second pass: the contents now carry page numbers (the contents keep their length, so the numbers stay right).
        write_html(body, entries, pages)
        if not print_pdf(browser):
            print('The browser did not write the PDF.')
            return 1
        print('[%s] page numbers added to the contents' % lang)
    else:
        print('[%s] Contents without page numbers (install PyMuPDF with: pip install pymupdf).' % lang)
    print('[%s] wrote %s (%.1f MB)' % (lang, OUT_PDF, os.path.getsize(OUT_PDF) / 1e6))
    return 0


def main():
    args = sys.argv[1:]
    langs = ['en']
    for i, a in enumerate(args):
        if a == '--lang' and i + 1 < len(args):
            langs = LANGS if args[i + 1] == 'all' else [args[i + 1]]
        elif a.startswith('--lang='):
            langs = LANGS if a[7:] == 'all' else [a[7:]]
    for lang in langs:
        if lang not in LANGS:
            print('Unknown language %r (use one of %s or all).' % (lang, ', '.join(LANGS)))
            return 2
    html_only = '--html' in args
    browser = None
    if not html_only:
        browser = find_browser()
        if not browser:
            print('No Edge or Chrome found; open the guide HTML in a browser and print it to PDF (A4, margins none, no headers).')
            return 1
    for lang in langs:
        rc = build(lang, html_only, browser)
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    sys.exit(main())
