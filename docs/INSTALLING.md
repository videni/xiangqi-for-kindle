# PW4 5.18：KPM 安装与构建

## 安装

设备需要已越狱并可运行 KPM，书库入口需要 Hotfix 的 Scriptlet 支持，不需要 KUAL。

下载 [v0.2.2 的 xiangqi.kpkg](https://github.com/videni/chinese-chess-for-kindle/releases/download/v0.2.2/xiangqi.kpkg)。
该 `kindlehf` 安装包已在 PW4 / 固件 5.18.1.1.1 / KPM 0.2.4 验证；其他机型尚未实测。

1. 把下载的 `xiangqi.kpkg` 复制到 Kindle USB 存储根目录。
2. 弹出 Kindle，在主页搜索栏输入并执行：

```text
;kpm install file:///mnt/us/xiangqi.kpkg
```

3. 安装后打开书库里的“象棋”。也可以在搜索栏执行：

```text
;kpm launch exactchinesechess
```

包平台为 `kindlehf`，manifest v2，包含应用、ARM 运行库、Pikafish 和 NNUE。
本地文件安装语法来自 KPM 的 `file://` install target。较旧的 KPM 如果不识别该语法，先按其官方流程升级 KPM。

## 交互

书库入口为 `/mnt/us/documents/chinese-chess.sh`，显示名称仍为“象棋”；升级只迁移与本包字节一致的旧入口，存档位置不变。

- 点棋子，再点落点；抬手才处理点击。
- 从屏幕底边约 120 像素内向上滑至少 96 像素，打开菜单。
- 轻点顶部状态区域、棋盘上方或下方的留白，也能打开底部菜单；棋盘内的空位仍用于走棋。
- 从屏幕顶部约 64 像素内向下滑至少 96 像素，打开 Kindle 原生快捷设置；平时保持全屏棋盘。
- 下滑菜单顶部的短横条，或点棋盘，收起菜单；本次触摸不走棋。
- 菜单只有同一排的“新局、悔棋、退出”，白底浮在棋盘上方；菜单、新局面板展开或收起，棋盘位置和大小均不变。
- 棋盘、状态和弹窗共用一个画布；菜单开关只请求面板区域重绘，横条和按钮同次绘制。
- 顶部平时只显示当前走棋方；成功续局先显示“已恢复上次对局”，两秒后恢复走棋方提示。
- 新局提供“我执红、我执黑、双人对弈”；双人对弈隐藏电脑难度。选项使用白底单选标记，确认开始后才改变棋局。
- 退出时自动保存棋局、执方和难度；下次打开自动续局。保存失败会保留窗口并提示重试。
- 棋局留在后台时，再点书库入口会唤回同一棋局，保持入口运行到棋局退出，不重复启动引擎。
- 已有存档读取失败时保留原文件，退出不会用空棋局覆盖；开始新局或成功载入后恢复自动保存。
- 新局设置点“开始新局”才生效；“取消”保留原局。
- 执方／玩法与难度使用水平单选项，以圆点标记选择，不弹出下拉框。
- 书库启动入口关闭 FBInk 的脚本输出，直接进入应用；诊断信息写入数据目录的 `scriptlet.log`。
- 书库封面使用黑白中国象棋图，通过 Scriptlet 前六行内的 `# Icon:` 内嵌 JPEG 设置。

## 存档与卸载

```text
/mnt/us/exact-chinesechess/game.save
/mnt/us/exact-chinesechess/app.log
/mnt/us/exact-chinesechess/launch.log
```

升级保留存档；卸载保留该数据目录。卸载命令：

```text
;kpm uninstall exactchinesechess
```

KPM 0.2.4 覆盖安装本地包若出现 `Could not resolve dependency graph`，先退出象棋完成自动保存，再用以上命令卸载应用，随后重新执行本地包安装命令。存档目录保留；该流程已在 PW4 验证。

旧版存档仍会从 `/mnt/us/extensions/exact-chinesechess/exact-chinesechess.save` 或 `/mnt/us/documents/exact-chinesechess.txt` 自动读取。

## 构建

在 macOS 开启 OrbStack / Docker。构建器使用 ARMv7 Debian Bullseye；其旧发行包来自 Debian 官方归档源。

```sh
cd ~/Workspace/exact-chinesechess
EXACT_CHINESECHESS_PACKAGE=0 bash ~/Workspace/exact-chinesechess/docker_rebuild.sh
```

下载上游 v0.1.3 的 `exact-chinesechess-extension.zip`，保存为 `~/Workspace/exact-chinesechess/release/upstream-v0.1.3.zip`。打包器会验证 SHA256，然后用本分支新编译的应用替换原应用，保留上游运行库、皮卡鱼、模型和许可证：

```sh
python3 ~/Workspace/exact-chinesechess/package_kpm.py --extension-zip ~/Workspace/exact-chinesechess/release/upstream-v0.1.3.zip
```

生成的安装包为 `~/Workspace/exact-chinesechess/release/xiangqi.kpkg`。分发时同时提供本分支完整源码和上游许可证；本次交付会附带 `source.tar.gz`。

## 验证

手势、安装生命周期和引擎测试分别覆盖触摸分类、存档保留、入口所有权、走棋和悔棋。GTK UI 集成测试在 1072×1448 的 Xvfb 中运行真实窗口，启用与 Kindle 一致的原生窗口模式，要求菜单开关各触发一次局部重绘、始终只有一个内容绘制窗口。另覆盖三按钮悬浮菜单、面板展开前后棋盘位置与大小、顶部及底部点击、手势隔离、新局取消、恢复提示计时、退出自动保存、重启续局和损坏存档保护。

本版已在 PW4 真机验证首页入口、封面、菜单展开收起、新局取消、系统快捷设置、自动续局及后台唤回。
本版进一步去掉弹窗的分层窗口，改为单画布局部更新；物理闪烁程度需以真机观察为准，窗口与重绘事件检查不能代替墨水屏手感验证。

References:
- https://kindlemodding.org/kindle-dev/kpm/creating-a-package.html
- https://github.com/KindleModding/KPM/blob/main/src/install.c
- https://github.com/jckhng/exact-chinesechess/releases/tag/v0.1.3
