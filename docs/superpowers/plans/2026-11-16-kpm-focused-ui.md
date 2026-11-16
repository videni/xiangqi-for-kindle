# KPM Focused Chess Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** PW4 5.18 使用 KPM 安装象棋，底边上滑才显示菜单。

**Architecture:** 保留原 GTK2/Cairo 和象棋引擎。将触摸分类提取为独立 C 模块，主窗口改为状态、棋盘、默认隐藏的操作区；KPM hooks 管理入口与包生命周期。

**Tech Stack:** C99, GTK2, Cairo, POSIX sh, Python unittest, Docker ARMv7 Debian.

**Spec:** ~/Workspace/exact-chinesechess/docs/superpowers/specs/2026-11-16-kpm-focused-ui-design.md

## Global Constraints

- 目标 PW4 5.18；包平台 kindlehf；不依赖 KUAL。
- 主屏仅棋盘和状态；菜单从底边上滑出现。
- 不修改棋规、AI 算法、棋子素材；保留 Pikafish。
- 升级与卸载保留存档；不发布、不推送。

## Review Focus

- 菜单手势在 AI 思考时也应工作。
- 滑动和关闭菜单不能让棋局多走一步。
- 取消新局设置不能重置对局。
- 从棋谱退出必须返回当前棋局。
- 安装目录变动、升级和卸载不能丢存档或删除用户文件。

### Task 1: 手势与专注主屏

**Files:** main.c, Makefile; create touch_input.h, touch_input.c, touch_input_test.c.

**Interfaces:** touch input state classifies a press/move/release as tap, swipe-up, swipe-down or ignored; GUI applies the classified action. `EXACT_CHINESECHESS_DATA_DIR` chooses writable state directory; `EXACT_CHINESECHESS_PIKAFISH` already selects engine binary.

- [x] Write hand-checked gesture cases; compile and watch missing implementation fail.
- [x] Implement touch state; validate bottom-edge admission, distance, horizontal rejection, cancellation and tap behavior.
- [x] Replace persistent controls with hidden menu; preserve callbacks and make new-game settings transactional.
- [x] Read save/log locations from writable data directory; retain legacy load support.
- [x] Run gesture and existing smoke tests; build GTK app and exercise real UI at PW4 size.
- [x] Commit scoped source/tests after verification.

### Task 2: KPM 包与启动

**Files:** create kpm/manifest.json, kpm/install.sh, kpm/launch.sh, kpm/uninstall.sh, kpm/scriptlets/xiangqi.sh, package_kpm.py, tests/test_kpm.py; update README.md, docs/INSTALLING.md.

**Interfaces:** accepts an existing extension payload containing ARM binary, lib, assets, Pikafish/NNUE; root launch exports `EXACT_CHINESECHESS_DATA_DIR` and `EXACT_CHINESECHESS_PIKAFISH` to the UI.

- [x] Write lifecycle tests that execute hooks in an isolated test root and inspect files/exit codes.
- [x] Implement relocatable package staging, hooks, library/PNG-loader setup and gzip-tar manifest v2 packaging.
- [x] Ensure upgrade preserves state and uninstall removes only matching owned scriptlets.
- [x] Run hook tests; inspect package manifest and payload; validate ARM ELF and required model.
- [x] Document local KPM install and launch commands based on the installed KPM interface.
- [x] Commit scoped packaging/docs after verification.

### Task 3: 集成与验收

**Files:** validation artifacts in ignored build directory, relevant fixes only.

**Interfaces:** uses final UI binary and final `.kpkg`.

- [x] Build Linux GTK executable with Xvfb, exercise bottom swipe, dismissal, new-game cancel and history exit; save screenshots.
- [x] Build ARMv7 executable, reuse release runtime/engine after checking SHA256, create final package.
- [x] Run focused tests and a fresh whole-branch review; address material findings.
- [x] Report package path, verification evidence and remaining device validation separately.
