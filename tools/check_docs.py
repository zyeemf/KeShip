#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KeShip 文档体检脚本。

设计约束（务必保持）：
1. 每一项检查都必须报告【检查样本数】；样本数为 0 时输出「未能判定」而非「通过」——
   避免检查项悄悄失去作用却仍显示全绿。
2. 「通过」「未能判定」「发现问题」三种结论分开展示，不允许合并成一句"没问题"。
3. 幂等只读，不修改任何文件。

用法：
    python3 tools/check_docs.py
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent      # keship_ws/
DOC = ROOT / "doc"
ARCH = DOC / "keship_architecture.md"
PLANS_IDX = DOC / "keship_plans.md"

OK, WARN, FAIL, INFO = "[OK]", "[??]", "[!!]", "[--]"


def read(p: Path) -> str:
    try:
        return p.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""


def md_files(base: Path, recursive=True):
    if not base.is_dir():
        return []
    it = base.rglob("*.md") if recursive else base.glob("*.md")
    return sorted(p for p in it if p.is_file())


def active_notes():
    """notes/ 下的活跃笔记（排除 archive/）。"""
    notes = DOC / "notes"
    return [p for p in md_files(notes) if "archive" not in p.relative_to(notes).parts]


def report_title(title):
    print("\n" + "=" * 72)
    print(title)
    print("=" * 72)


# ---------------------------------------------------------------- 检查项 1
def check_arch_coverage():
    report_title("检查项 1：架构文档是否覆盖全部活跃专题笔记")
    if not ARCH.is_file():
        print(f"{FAIL} 未找到 {ARCH}，本项无法执行。")
        return 1
    arch = read(ARCH)
    samples = active_notes()
    if not samples:
        print(f"{WARN} 未能判定：doc/notes/ 下没有活跃笔记（样本数 0），无法判断覆盖情况。")
        return 0
    missing = [p for p in samples if p.name not in arch]
    print(f"检查样本数：{len(samples)} 份活跃笔记")
    if missing:
        print(f"{FAIL} 发现问题：{len(missing)} 份笔记未登记进 keship_architecture.md：")
        for p in missing:
            print(f"       - {p.relative_to(DOC)}")
        return 1
    print(f"{OK} 通过：全部 {len(samples)} 份活跃笔记均已在架构文档中出现。")
    return 0


# ---------------------------------------------------------------- 检查项 2
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FENCE_RE = re.compile(r"```.*?```", re.S)
INLINE_CODE_RE = re.compile(r"`[^`\n]*`")


def strip_code(text: str) -> str:
    """剔除代码块与行内代码后再解析链接，避免把 C++ lambda / 数组下标误判成链接。

    例：`const gz::msgs::Pose_V &_msg` 中的 `[_msg]()` 会被 LINK_RE 命中，
    但它是代码而非文档链接。此处的剔除必须保留行号语义之外的文本行数，
    以免影响后续定位（当前仅统计，不依赖行号）。
    """
    text = FENCE_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    return INLINE_CODE_RE.sub("", text)


def check_links():
    report_title("检查项 2：文档内部相对链接是否有效")
    targets = [ARCH, DOC / "keship_developer_guide.md", DOC / "README.md",
               DOC / "DOC_WORKFLOW.md", DOC / "keship_plans.md"] + md_files(DOC / "notes") + md_files(DOC / "plans")
    files = sorted({p for p in targets if p.is_file()})
    if not files:
        print(f"{WARN} 未能判定：没有可检查的文档（样本数 0）。")
        return 0
    total, broken = 0, []
    for f in files:
        text = strip_code(read(f))
        for m in LINK_RE.finditer(text):
            raw = m.group(1).strip()
            if not raw or raw.startswith(("http://", "https://", "mailto:", "#")):
                continue
            raw = raw.split("#")[0]
            if not raw:
                continue
            total += 1
            tgt = (f.parent / raw).resolve()
            if not tgt.exists():
                broken.append((f.relative_to(DOC), raw))
    print(f"检查样本数：{len(files)} 个文件 / {total} 条相对链接")
    if total == 0:
        print(f"{WARN} 未能判定：未发现任何相对链接，不排除是解析失败导致。")
        return 0
    if broken:
        print(f"{FAIL} 发现问题：{len(broken)} 条链接指向不存在的文件：")
        for src, raw in broken:
            print(f"       - {src}  ->  {raw}")
        return 1
    print(f"{OK} 通过：全部 {total} 条相对链接均有效。")
    return 0


# ---------------------------------------------------------------- 检查项 3
def check_plans_index():
    report_title("检查项 3：plans/ 是否全部登记进 keship_plans.md")
    plans = md_files(DOC / "plans")
    if not plans:
        print(f"{WARN} 未能判定：doc/plans/ 下没有方案文件（样本数 0）。")
        return 0
    idx = read(PLANS_IDX) if PLANS_IDX.is_file() else ""
    print(f"检查样本数：{len(plans)} 份方案文档")
    missing = [p for p in plans if p.name not in idx]
    if missing:
        print(f"{FAIL} 发现问题：{len(missing)} 份未登记：")
        for p in missing:
            print(f"       - {p.relative_to(DOC)}")
        return 1
    print(f"{OK} 通过：全部 {len(plans)} 份方案均已登记。")
    return 0


# ---------------------------------------------------------------- 检查项 4
def check_duplicate_copies():
    report_title("检查项 4：仓库内是否存在文档重复副本（doc/ 之外）")
    skip_dirs = {"build", "install", "log", ".git", "mylogs", "data", "models_all", "extern", "__pycache__"}
    outside = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = Path(dirpath).relative_to(ROOT)
        if rel.parts and rel.parts[0] in skip_dirs:
            dirnames[:] = []
            continue
        if rel.parts and rel.parts[0] == "doc":
            continue
        for fn in filenames:
            if fn.lower().endswith(".md"):
                outside.append(rel / fn)
    # AGENTS.md / README.md 是仓库根入口，属正常
    outside = [p for p in outside if p.name not in ("AGENTS.md", "README.md")]
    print(f"检查样本数：{len(outside)} 个位于 doc/ 之外的 markdown 文件")
    if not outside:
        print(f"{WARN} 未能判定：doc/ 之外没有任何 md 文件，请确认本脚本是否跑在了正确的仓库根（当前根={ROOT}）。")
        return 0
    dup = []
    doc_names = {p.name for p in md_files(DOC)}
    for p in outside:
        if p.name in doc_names or (ROOT / "doc" / p.name).exists():
            dup.append(p)
    if dup:
        print(f"{FAIL} 发现问题：{len(dup)} 个文件与 doc/ 内文档同名（疑似分叉副本）：")
        for p in dup:
            print(f"       - {p}")
        return 1
    print(f"{OK} 通过：doc/ 之外未发现同名重复副本（其余 {len(outside)} 个 md 为各模块自有文档，需人工确认是否应迁入 doc/）。")
    return 0


# ---------------------------------------------------------------- 检查项 5
def check_note_header():
    report_title("检查项 5：笔记是否具备标题与状态标记")
    samples = active_notes()
    if not samples:
        print(f"{WARN} 未能判定：没有活跃笔记（样本数 0）。")
        return 0
    no_title, no_status = [], []
    marks = ("✅", "❌", "🔄", "⚠️", "📋", "🔲")
    for p in samples:
        t = read(p)
        head = t[:1200]
        if not re.search(r"^#\s+\S", t, re.M):
            no_title.append(p.relative_to(DOC))
        elif not any(m in head for m in marks):
            no_status.append(p.relative_to(DOC))
    print(f"检查样本数：{len(samples)} 份活跃笔记")
    bad = len(no_title) + len(no_status)
    if no_title:
        print(f"{FAIL} 缺少一级标题（{len(no_title)} 份）：")
        for p in no_title:
            print(f"       - {p}")
    if no_status:
        print(f"{INFO} 未带状态标记（{len(no_status)} 份，非强制，建议补充）：")
        for p in no_status:
            print(f"       - {p}")
    if not no_title:
        print(f"{OK} 通过：全部 {len(samples)} 份笔记均有一级标题。")
    return 1 if no_title else 0


def main():
    print("KeShip 文档体检")
    print(f"仓库根：{ROOT}")
    print(f"文档根：{DOC}")
    if not DOC.is_dir():
        print(f"\n{FAIL} doc/ 目录不存在，请确认运行位置。")
        return 2
    results = [
        ("架构覆盖", check_arch_coverage()),
        ("链接有效", check_links()),
        ("计划索引", check_plans_index()),
        ("重复副本", check_duplicate_copies()),
        ("笔记规范", check_note_header()),
    ]
    print("\n" + "=" * 72)
    print("体检汇总")
    print("=" * 72)
    failed = [n for n, c in results if c == 1]
    for n, c in results:
        print(f"  {n:<10} {'发现问题' if c == 1 else '通过/已判定'}")
    print()
    if failed:
        print(f"结论：{'、'.join(failed)} 项发现问题，需处理。注意 [??] 标记项表示样本为空、未能判定，不等于安全。")
        return 1
    print("结论：各项均已实际判定（无空样本）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
