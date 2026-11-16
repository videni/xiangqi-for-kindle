# PW4 5.18：KPM 安装与构建

## 安装

设备需要已越狱并可运行 KPM，不需要 KUAL。

1. 把本次生成的 `xiangqi.kpkg` 复制到 Kindle USB 存储根目录。
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

- 点棋子，再点落点；抬手才处理点击。
- 从屏幕底边约 120 像素内向上滑至少 96 像素，打开菜单。
- 轻点棋盘下方的底部留白，也能打开菜单；棋盘内的空位仍用于走棋。
- 从屏幕顶部约 64 像素内向下滑至少 96 像素，打开 Kindle 原生快捷设置；平时保持全屏棋盘。
- 下滑菜单顶部的收起区，或点棋盘，收起菜单；本次触摸不走棋。
- 新局、悔棋、保存、载入、棋谱、退出都在菜单里。
- 新局设置点“开始新局”才生效；“返回”保留原局。
- 执方／玩法与难度使用水平单选项，点选反白，不弹出下拉框。
- 关闭棋谱后自动回到当前对局。
- 书库启动入口关闭 FBInk 的脚本输出，直接进入应用；诊断信息写入数据目录的 `scriptlet.log`。

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

KPM 0.2.4 覆盖安装本地包若出现 `Could not resolve dependency graph`，先在象棋菜单保存，再用以上命令卸载应用，随后重新执行本地包安装命令。存档目录保留；该流程已在 PW4 验证。

旧版存档仍可从 `/mnt/us/extensions/exact-chinesechess/exact-chinesechess.save` 或 `/mnt/us/documents/exact-chinesechess.txt` 载入。

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

手势、安装生命周期和引擎测试分别覆盖触摸分类、存档保留、入口所有权、走棋和悔棋。GTK UI 集成测试在 1072×1448 的 Xvfb 中运行真实窗口，覆盖隐藏菜单、滑动不落子、新局取消、棋谱退出和存档往返。

这些检查不代替 PW4 真机验收。设备安装、墨水屏刷新、触控手感、真实 AI 耗时仍需在用户设备上验证。

References:
- https://kindlemodding.org/kindle-dev/kpm/creating-a-package.html
- https://github.com/KindleModding/KPM/blob/main/src/install.c
- https://github.com/jckhng/exact-chinesechess/releases/tag/v0.1.3
