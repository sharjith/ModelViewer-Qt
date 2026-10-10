#!/usr/bin/env python3
"""Builds the Simulation Results and 3D Plotting guide PDF from the HTML chapters in parts/.

    python build_guide.py            builds guide.html and the PDF
    python build_guide.py --html     only guide.html (open it in a browser to check the layout)

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
OUT_HTML = os.path.join(HERE, 'guide.html')
OUT_PDF = os.path.join(HERE, 'ModelViewer_Simulation_and_3D_Plotting_Guide.pdf')

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
    parts_dir = os.path.join(HERE, 'parts')
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
    out = ['<section class="toc"><h1>Contents</h1><ol>']
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


def write_html(body_template, entries, pages=None):
    body = body_template.replace('<!--TOC-->', build_toc(entries, pages))
    doc = ('<!DOCTYPE html>' + NL + '<html lang="en"><head><meta charset="utf-8">'
           '<title>ModelViewer - Simulation Results and 3D Plotting Guide</title>'
           '<link rel="stylesheet" href="guide.css"></head><body>' + NL + body + NL + '</body></html>' + NL)
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


def main():
    body = read_parts()
    body, kept, dropped = resolve_figures(body)
    body, entries = number_headings(body)
    write_html(body, entries)
    print('guide.html: %d chapters/sections listed, %d pictures included, %d not captured yet (their figures are left out)'
          % (len(entries), kept, dropped))
    if '--html' in sys.argv:
        return 0
    browser = find_browser()
    if not browser:
        print('No Edge or Chrome found; open guide.html in a browser and print it to PDF (A4, margins none, no headers).')
        return 1
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
        print('page numbers added to the contents')
    else:
        print('Contents without page numbers (install PyMuPDF with: pip install pymupdf).')
    print('wrote', OUT_PDF, '(%.1f MB)' % (os.path.getsize(OUT_PDF) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
