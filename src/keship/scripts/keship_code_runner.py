#!/usr/bin/env python3
"""KeShip 学生编程沙箱 — Python / C 代码执行器

用法:
  python3 keship_code_runner.py python '<code>'
  python3 keship_code_runner.py c      '<code>'

输入: 语言, 学生完整代码（含数据声明）
输出: JSON {ok: bool, stdout: str, stderr: str}
"""

import subprocess, sys, json, os


def run_python(code: str, timeout: int = 10) -> dict:
    """执行完整 Python 代码"""
    try:
        r = subprocess.run(
            [sys.executable, '-c', code],
            capture_output=True, text=True, timeout=timeout,
            cwd='/tmp',
            env={'HOME': '/tmp', 'PATH': '/usr/bin', 'LANG': 'C.UTF-8'})
        return {'ok': r.returncode == 0,
                'stdout': r.stdout.strip(),
                'stderr': r.stderr.strip()}
    except subprocess.TimeoutExpired:
        return {'ok': False, 'stderr': '代码执行超时'}


def run_c(code: str, timeout: int = 15) -> dict:
    """编译并执行完整 C 代码"""
    src_path = '/tmp/keship_student_code.c'
    bin_path = '/tmp/keship_student_prog'
    with open(src_path, 'w') as f:
        f.write(code)

    try:
        cr = subprocess.run(
            ['gcc', '-o', bin_path, src_path, '-lm', '-Wall', '-Werror'],
            capture_output=True, text=True, timeout=10)
    except FileNotFoundError:
        return {'ok': False, 'stderr': '系统未安装 gcc'}
    except subprocess.TimeoutExpired:
        return {'ok': False, 'stderr': '编译超时'}

    if cr.returncode != 0:
        os.unlink(src_path)
        return {'ok': False, 'stderr': '编译失败:\n' + cr.stderr}

    try:
        rr = subprocess.run(
            [bin_path],
            capture_output=True, text=True, timeout=5,
            cwd='/tmp',
            env={'HOME': '/tmp', 'PATH': '/usr/bin', 'LANG': 'C.UTF-8'})
        if os.path.exists(src_path): os.unlink(src_path)
        if os.path.exists(bin_path): os.unlink(bin_path)
        return {'ok': rr.returncode == 0,
                'stdout': rr.stdout.strip(),
                'stderr': rr.stderr.strip()}
    except subprocess.TimeoutExpired:
        return {'ok': False, 'stderr': '程序运行超时'}


def main():
    if len(sys.argv) < 3:
        print(json.dumps({'ok': False, 'stderr': '参数不足'}))
        sys.exit(1)

    language = sys.argv[1]
    code = sys.argv[2]

    if language == 'python':
        result = run_python(code)
    elif language == 'c':
        result = run_c(code)
    else:
        result = {'ok': False, 'stderr': f'不支持的语言: {language}'}

    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
