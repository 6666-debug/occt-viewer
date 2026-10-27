#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""仓库自检：不依赖 Qt / OCCT 的轻量检查，用于 CI 和本地提交前校验。

覆盖：
  1. 关键文件齐全（源码、资源、截图、构建脚本）
  2. src/ 下源码 UTF-8 无 BOM、LF 换行
  3. 三个头文件的 include guard 正确
  4. 不使用 Q_OBJECT（项目用回调代替信号槽，避免 moc 依赖）
  5. CMakeLists.txt 里的源文件/资源清单与磁盘一致
  6. README 引用的图片确实存在
  7. resources.qrc 与 .gitignore 的关键条目存在
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

REQUIRED_FILES = [
    "CMakeLists.txt",
    "README.md",
    "LICENSE",
    "deploy.ps1",
    "resources.qrc",
    "src/main.cpp",
    "src/Types.h",
    "src/OcctViewport.h",
    "src/OcctViewport.cpp",
    "src/MainWindow.h",
    "src/MainWindow.cpp",
    "resources/app.ico",
    "resources/samples/as1_motor.step",
    "screenshots/main.png",
    "screenshots/measure.png",
    "tests/tst_measure.cpp",
]

EXPECTED_GUARDS = {
    "src/Types.h": "TYPES_H",
    "src/OcctViewport.h": "OCCTVIEWPORT_H",
    "src/MainWindow.h": "MAINWINDOW_H",
}

errors: list[str] = []


def check_required_files() -> None:
    for rel in REQUIRED_FILES:
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"缺少文件：{rel}")
        elif path.stat().st_size == 0:
            errors.append(f"文件为空：{rel}")


def check_source_encoding() -> None:
    for path in sorted((ROOT / "src").glob("*")):
        if path.suffix not in {".h", ".cpp"}:
            continue
        data = path.read_bytes()
        rel: str = str(path.relative_to(ROOT))
        if data.startswith(b"\xef\xbb\xbf"):
            errors.append(f"{rel} 带 UTF-8 BOM（MSVC /utf-8 下会出现乱码隐患）")
        if b"\r\n" in data:
            errors.append(f"{rel} 使用 CRLF（源码统一 LF，.gitattributes 已约定）")
        try:
            data.decode("utf-8")
        except UnicodeDecodeError as exc:
            errors.append(f"{rel} 不是合法 UTF-8：{exc}")


def check_include_guards() -> None:
    for rel, guard in EXPECTED_GUARDS.items():
        text = (ROOT / rel).read_text(encoding="utf-8")
        if f"#ifndef {guard}" not in text or f"#define {guard}" not in text:
            errors.append(f"{rel} 缺少 include guard {guard}")
        if f"#endif // {guard}" not in text:
            errors.append(f"{rel} 尾部缺少 #endif // {guard} 注释")
        if "#pragma once" in text:
            errors.append(f"{rel} 同时使用 #pragma once 与 include guard")


def check_no_qobject() -> None:
    for path in sorted((ROOT / "src").glob("*")):
        if path.suffix not in {".h", ".cpp"}:
            continue
        text = path.read_text(encoding="utf-8")
        if re.search(r"^\s*Q_OBJECT\s*$", text, re.MULTILINE):
            errors.append(f"{path.name} 使用了 Q_OBJECT（本项目约定用 std::function 回调）")


def check_cmake() -> None:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    for rel in ["src/main.cpp", "src/OcctViewport.cpp", "src/MainWindow.cpp"]:
        if rel not in text:
            errors.append(f"CMakeLists.txt 未引用 {rel}")
    for marker in ["qt_add_resources", "qt_add_executable", "find_package(OpenCASCADE"]:
        if marker not in text:
            errors.append(f"CMakeLists.txt 缺少关键片段：{marker}")
    for rel in ["resources/app.ico", "resources/samples/as1_motor.step"]:
        if rel not in text:
            errors.append(f"CMakeLists.txt 未打包资源 {rel}")


def check_readme_images() -> None:
    text = (ROOT / "README.md").read_text(encoding="utf-8")
    for match in re.finditer(r"!\[[^\]]*\]\(([^)]+)\)", text):
        target = match.group(1).strip()
        if target.startswith(("http://", "https://")):
            continue
        if not (ROOT / target).is_file():
            errors.append(f"README 引用的图片不存在：{target}")


def check_qrc() -> None:
    text = (ROOT / "resources.qrc").read_text(encoding="utf-8")
    for match in re.finditer(r"<file>([^<]+)</file>", text):
        target = match.group(1).strip()
        if not (ROOT / target).is_file():
            errors.append(f"resources.qrc 声明的文件不存在：{target}")


def check_gitignore() -> None:
    text = (ROOT / ".gitignore").read_text(encoding="utf-8")
    lines = {line.strip() for line in text.splitlines()}
    for pattern in ["build*/", "dist*/", "release/"]:
        if pattern not in lines:
            errors.append(f".gitignore 缺少 {pattern}")


def main() -> int:
    check_required_files()
    check_source_encoding()
    check_include_guards()
    check_no_qobject()
    check_cmake()
    check_readme_images()
    check_qrc()
    check_gitignore()

    if errors:
        print(f"仓库自检失败，共 {len(errors)} 项：")
        for item in errors:
            print(f"  - {item}")
        return 1

    print("仓库自检通过：文件齐全、源码 UTF-8/LF、include guard 正确、无 Q_OBJECT、构建与文档引用一致。")
    return 0


if __name__ == "__main__":
    sys.exit(main())