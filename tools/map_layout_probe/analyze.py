"""地图底部文字行分布分析 — 判定「X 轴刻度」与「操作提示」是否重叠。

做法：只扫描每张图底部 60px（避开绘图区的轴框与多边形线条），
逐行统计深色像素数，把连续的非空行归为一个「文字行带」。
若刻度与提示重叠，二者会合并成**一个**带；修复后应分离为**独立的多个**带。
"""
import sys
from PIL import Image

DARK = 170          # 深色阈值（文字为黑/深灰，背景 245,248,252，网格 220）
MIN_PIX = 5         # 一行至少这么多深色像素才算文字
BAND_TOP = 60       # 只看底部 60px


def bands(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    y0 = h - BAND_TOP
    rows = []
    for y in range(y0, h):
        n = 0
        for x in range(w):
            r, g, b = px[x, y]
            if r < DARK and g < DARK and b < DARK:
                n += 1
        rows.append((y, n))

    out, start, last, peak = [], None, None, 0
    for y, n in rows:
        if n >= MIN_PIX:
            if start is None:
                start, peak = y, n
            last = y
            peak = max(peak, n)
        else:
            if start is not None:
                out.append((start - y0, last - y0, last - start + 1, peak))
                start = None
    if start is not None:
        out.append((start - y0, last - y0, last - start + 1, peak))
    return w, h, out


def main():
    for path in sys.argv[1:]:
        w, h, bs = bands(path)
        print("== %s  (%dx%d) ==" % (path, w, h))
        if not bs:
            print("   （底部 60px 内未检出文字）")
            continue
        for i, (a, b, hgt, peak) in enumerate(bs, 1):
            print("   带%d: 底部偏移 y=%2d~%2d  高 %2dpx  峰值深色像素=%d"
                  % (i, a, b, hgt, peak))
        verdict = ("分离（无重叠）" if len(bs) >= 3
                   else "疑似重叠（文字行带过少）" if len(bs) <= 2
                   else "—")
        print("   → 底部文字行带数 = %d   %s" % (len(bs), verdict))
        print()


main()
