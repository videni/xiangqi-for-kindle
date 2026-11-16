# Xiangqi for Kindle

面向越狱 Kindle 的中国象棋，支持 KPM 安装、Pikafish 离线对弈和自动续局。

**[下载 v0.2.3 安装包](https://github.com/videni/xiangqi-for-kindle/releases/download/v0.2.3/xiangqi.kpkg)** · [发布说明](https://github.com/videni/xiangqi-for-kindle/releases/tag/v0.2.3)

## 界面

PW4 真机界面：

<img src="docs/images/kindle-game.png" alt="象棋对局界面" width="340"> <img src="docs/images/kindle-menu.png" alt="新局、悔棋、退出工具栏" width="340">

## 功能与操作

- 对弈方式：我执红、我执黑、双人对弈。
- 电脑难度：简单、中等、困难。
- 点选棋子，再点落点走棋。
- 底边上滑，或轻点顶部状态、底部空白区域，打开“新局、悔棋、退出”工具栏。
- 点棋盘或向下滑动，收起工具栏。
- 顶边向下滑动，打开 Kindle 系统快捷设置。
- 退出时自动保存，启动时自动恢复上次对局。
- 书库入口显示中国象棋封面，名称为“象棋”。

## KPM 安装

需要已越狱的 Kindle、KPM，以及 Hotfix 提供的 Scriptlet 支持。
已在 **PW4 / 固件 5.18.1.1.1 / KPM 0.2.4** 上实测。

1. 下载上方的 `xiangqi.kpkg`，复制到 Kindle USB 存储根目录，安全弹出设备。
2. 在 Kindle 首页搜索栏输入并执行：

```text
;kpm install file:///mnt/us/xiangqi.kpkg
```

3. 安装后打开书库里的“象棋”，或在搜索栏执行：

```text
;kpm launch xiangqi
```

安装包包含应用、ARM 运行库、Pikafish 和 NNUE。
发布页同时提供源码、安装说明和 SHA256 校验文件。

存档位于 `/mnt/us/exact-chinesechess/game.save`，升级和卸载会保留存档。
更新与构建说明见 [安装文档](docs/INSTALLING.md)。

PW4 上菜单开关会出现一次黑白刷新。

## 原项目资料

本项目基于 [jckhng/exact-chinesechess](https://github.com/jckhng/exact-chinesechess) 的 Kindle v0.1.3。以下为原版界面、引擎和 KUAL 构建资料。

A Kindle-friendly Xiangqi / Chinese Chess app using the same native GTK2/Cairo
and KUAL packaging pattern as Kindle GlChess and Exact Reversi.

### 原版界面

![Exact Chinese Chess screenshot](exact-chinesechess.png)

### Pikafish

The release package can include a Kindle ARMv7 hard-float build of Pikafish for
stronger AI play. If Pikafish is unavailable or fails to start, the app falls
back to the embedded AI automatically.

For maintainers building from source:

```bash
git submodule update --init --recursive
./package_extension.sh
```

Detailed Pikafish ARM build notes are in
[docs/PIKAFISH_ARM32_BUILD.md](docs/PIKAFISH_ARM32_BUILD.md).

#### Setting Up The Pikafish Submodule

For this repository, Pikafish is intended to live as a pinned Git submodule at
`Pikafish/`:

```bash
git submodule add https://github.com/official-pikafish/Pikafish Pikafish
git -C Pikafish checkout 76239d0b06720bfa4588989fd4ac7573e9dbf887
git add .gitmodules Pikafish
```

Future clones should use:

```bash
git clone --recurse-submodules <repo-url>
```

For an existing clone:

```bash
git submodule update --init --recursive
```

### Legacy KUAL build

```bash
git submodule update --init --recursive
./docker_rebuild.sh
./build_pikafish.sh
./package_extension.sh
```

The output package is:

```text
release/exact-chinesechess-extension.zip
```

Unzip it at the Kindle USB-storage root so it creates:

```text
/mnt/us/extensions/exact-chinesechess
/mnt/us/documents/shortcut_exactchinesechess.sh
```

Then launch from KUAL:

```text
KUAL -> Exact Chinese Chess -> Launch
```

### License And Provenance

Rules and built-in AI reference: XMuli ChineseChess, GPL-3.0-or-later,
<https://github.com/XMuli/ChineseChess>.

Artwork: Augus1217 Chinese-Chess, MIT per source project metadata,
<https://github.com/Augus1217/Chinese-Chess>.

Optional engine: Pikafish, GPLv3,
<https://github.com/official-pikafish/Pikafish>. When the release zip bundles
Pikafish, `LICENSES/PIKAFISH-SOURCE.txt` records the exact upstream commit and
build/source details.
