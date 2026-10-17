#!/usr/bin/env python3
"""Compare headless output PNGs with reference_shots: cmp_ref.py OUTDIR REFDIR -> per-shot diff pixel counts."""
import sys, os
from PIL import Image, ImageChops
out, ref = sys.argv[1], sys.argv[2]
res = []
for n in sorted(os.listdir(ref)):
    a = os.path.join(out, n)
    if not os.path.exists(a):
        res.append((n, 'missing')); continue
    ia = Image.open(a).convert('RGB'); ib = Image.open(os.path.join(ref, n)).convert('RGB')
    d = ImageChops.difference(ia, ib).convert('L').point(lambda v: 255 if v else 0)
    cnt = sum(1 for v in (d.get_flattened_data() if hasattr(d, "get_flattened_data") else d.getdata()) if v)
    bbox = d.getbbox()
    res.append((n, cnt, bbox))
for r in res: print(*r)
