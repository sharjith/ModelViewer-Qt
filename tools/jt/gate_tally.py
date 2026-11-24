#!/usr/bin/env python3
# Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
"""Counts how many XT streams pass the JT exact-geometry completeness gate (src/Import/Parasolid/XtImport.cpp, shapeFromJtElement).

    python -I tools/jt/gate_tally.py out/jt-research/gate.txt        (written by tools/jt/gate_scan.sh)

A stream passes when every face and edge was built, no tolerant edge was skipped, no direction conflict, unsupported transform, missing
occurrence or diagnostic was recorded, every body was placed, and no face was capped over a hole. Prints the totals, the totals per
sample (the part of the stream name before the first underscore) and the first failing streams with their reasons."""
import collections
import re
import sys

rows = [line.rstrip('\n') for line in open(sys.argv[1], encoding='utf-8')]
per_sample = collections.defaultdict(lambda: [0, 0])
reasons_total = collections.Counter()
failing = []
for line in rows:
    name, _, rest = line.partition(': ')
    numbers = {}
    for part in rest.split(', '):
        key, _, value = part.rpartition(' ')
        numbers[key] = value
    reasons = []
    if not rest:
        reasons.append('no output')
    else:
        for key in ('faces', 'edges', 'bodies'):
            built, _, total = numbers.get(key, '0/0').partition('/')
            if built != total:
                reasons.append(key + ' ' + numbers[key])
        for key in ('tolerant-skipped', 'direction conflicts', 'unsupported transform', 'missing occurrences', 'diagnostics', 'capped holes'):
            if numbers.get(key, '0') != '0':
                reasons.append(key + ' ' + numbers[key])
        if numbers.get('instances', '0') == '0':
            reasons.append('no instances')
    sample = name.split('_')[0]
    per_sample[sample][1] += 1
    if reasons:
        failing.append((name, reasons))
        for reason in reasons:
            reasons_total[re.sub(r' \d+(/\d+)?$', '', reason)] += 1
    else:
        per_sample[sample][0] += 1
print('streams', len(rows), 'pass', len(rows) - len(failing), 'fail', len(failing))
print(dict(reasons_total))
for sample, (passed, total) in sorted(per_sample.items()):
    print(sample, 'pass %d of %d' % (passed, total))
for name, reasons in failing[:25]:
    print(name, '->', ', '.join(reasons))
