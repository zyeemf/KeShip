#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 SD-10「船型说明」的公式 SVG 头文件。

背景（2026-11-03，张老师反馈"公式是文本格式，不像 LaTeX 好看"）：
  Qt 富文本引擎不支持 LaTeX / MathML，只能显示纯文本或上下标。
  系统也没有装 LaTeX（latex/dvipng/dvisvgm 全缺）。
  解法：用 matplotlib 的 mathtext 引擎**离线**把公式渲染成 SVG，
  字形转成 <path>（svg.fonttype='path'）⇒ 运行时不依赖任何字体 / 外部程序。
  SVG 以字符串常量编进代码，运行时由已有的 libqsvg 图像插件转位图内嵌。

用法：
    python3 tools/gen_sd10_equations.py
输出：
    src/keship/src_cpp/gui/keship/experiments/Sd10EquationSvg.hh

⚠ 幂等：重复运行结果一致（matplotlib 版本变化会改变 path 数据，属预期）。
"""
import io
import os
import re

import matplotlib
matplotlib.use("Agg")
matplotlib.rcParams["svg.fonttype"] = "path"    # 字形转 path：转位图不依赖字体
matplotlib.rcParams["mathtext.fontset"] = "cm"  # Computer Modern，LaTeX 观感
from matplotlib.figure import Figure

OUT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "src", "keship", "src_cpp", "gui", "keship", "experiments",
    "Sd10EquationSvg.hh")

# 变量名 -> (LaTeX, 字号)
EQS = [
    # ── 一、通用形式（整块 .formula）
    ("kGeneralForm",
     r"$y(x,z)=\dfrac{B}{2}\,W(\xi)\,S(\zeta)\qquad "
     r"\xi=\dfrac{2x}{L}-1\in[-1,1],\quad \zeta=-\dfrac{z}{D}\in[0,1]$", 19),
    # ── 三、Journée 四模型主公式
    ("kJournee",
     r"$y=(1-\zeta^{2})(1-\xi^{2})(1+a_2\xi^{2}+a_4\xi^{4})"
     r"+\alpha\,\zeta^{2}(1-\zeta^{8})(1-\xi^{2})^{4}$", 19),
    # ── 自检三段（note 块内，居中成行）
    ("kSelfCheckA1",
     r"$y(0,\zeta)=(1-\zeta^{2})+\zeta^{2}(1-\zeta^{8})=1-\zeta^{10}$", 17),
    ("kSelfCheckA1Int",
     r"$\int_{0}^{1}(1-\zeta^{10})\,d\zeta=\dfrac{10}{11}=0.9091"
     r"\;\Rightarrow\;C_m=0.909$", 17),
    ("kSelfCheckA0",
     r"$y(0,\zeta)=1-\zeta^{2},\quad \int_{0}^{1}=\dfrac{2}{3}"
     r"\;\Rightarrow\;C_m=0.667$", 17),
    # ── 表格单元格内的小表达式（W 列 / S 列）
    ("kW2", r"$1-\xi^{2}$", 12),
    ("kW4", r"$1-\xi^{4}$", 12),
    ("kS2", r"$1-\zeta^{2}$", 12),
    ("kS4", r"$1-\zeta^{4}$", 12),
    ("kS1", r"$1-\zeta$", 12),
    ("kSsqrt", r"$\sqrt{1-\zeta^{2}}$", 12),
]


def tex_to_svg(tex, fontsize):
    """mathtext -> 裁剪到内容的透明背景 SVG 字符串。"""
    fig = Figure(figsize=(12, 2), dpi=100)
    fig.patch.set_alpha(0)
    fig.text(0.01, 0.5, tex, fontsize=fontsize, va="center")
    buf = io.StringIO()
    fig.savefig(buf, format="svg", bbox_inches="tight",
                pad_inches=0.02, transparent=True)
    svg = buf.getvalue()
    # matplotlib 输出 width="360pt"；Qt 的 SVG 解析器会按 72dpi 把 pt 换成像素
    # （574pt -> 41328px，实测），故去掉单位让 Qt 按 viewBox 尺寸渲染。
    svg = re.sub(r'(width|height)="([\d.]+)pt"', r'\1="\2"', svg, count=2)
    # 去掉 XML 声明与 DOCTYPE：内联进 HTML 时不需要，且 Qt 解析更干净
    svg = re.sub(r'<\?xml[^>]*\?>\s*', '', svg)
    svg = re.sub(r'<!DOCTYPE[^>]*>\s*', '', svg)
    return svg.strip()


def main():
    parts = []
    for name, tex, size in EQS:
        svg = tex_to_svg(tex, size)
        # SVG 内容不得含 )SVG" ，否则会截断原始字符串
        assert ')SVG"' not in svg, name
        parts.append((name, tex, svg))
        print("%-16s %6d B  %s" % (name, len(svg), tex[:52]))

    lines = [
        "/*",
        " * Copyright (C) 2024-2026 KeShip Team",
        " */",
        "/* SD-10 船型说明公式（**自动生成，请勿手改**）",
        " *",
        " * 由 tools/gen_sd10_equations.py 生成：matplotlib mathtext -> SVG（字形已转 path）。",
        " * 重新生成：python3 tools/gen_sd10_equations.py",
        " *",
        " * 为什么是图片而不是文本：Qt 富文本引擎不支持 LaTeX / MathML，纯文本公式",
        " * （如 \"1 - zeta^2\"）既不美观也不易读；系统亦无 LaTeX。故离线渲染成矢量图，",
        " * 运行时由 libqsvg 图像插件（libqt5svg5）转位图，零字体 / 零外部程序依赖。",
        " */",
        "",
        "#ifndef SD10_EQUATION_SVG_HH_",
        "#define SD10_EQUATION_SVG_HH_",
        "",
        "namespace keship {",
        "namespace sd10_eq {",
        "",
    ]
    for name, tex, svg in parts:
        lines.append("/// %s" % tex)
        lines.append('constexpr const char *%s = R"SVG(%s)SVG";' % (name, svg))
        lines.append("")
    lines += [
        "}  // namespace sd10_eq",
        "}  // namespace keship",
        "",
        "#endif  // SD10_EQUATION_SVG_HH_",
        "",
    ]
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("\n写出 %s（%d 个公式，%d 字节）" % (OUT, len(parts), os.path.getsize(OUT)))


if __name__ == "__main__":
    main()
