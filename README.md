# contest2026_087_gaiduimingyizhanyongdui

👋 欢迎参加 **2026 首届 openvela AI 硬件开发者大赛**！

这是组委会为你的队伍创建的**专属参赛仓库**（本仓为样例/模板，队伍编号 `087`；你看到的将是你自己的 `contest2026_<编号>_<队伍名>`）。比赛期间，你的全部参赛代码、打包产物与 AI Coding 日志都提交到这里。

> 本仓既是「代码仓」，又内置了一键拉取整套 openvela 工程的 `repo` 清单（manifest）。你只需跟它打交道，**自始至终只动一个文件夹**。

---

## 一、先读这些官方文档

**通用（所有赛道必读）：**

| 文档                                                                                                                                     | 用途                                           |
| ---------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| [《大赛总览》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)                        | 赛道、流程、评分、资源，建议先通读             |
| [《参赛代码提交指南》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)           | 仓库获取、提交流程、时间与权限（**以此为准**） |
| [《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md) | 如何导出 AI 对话日志并提交到 `logs/`           |

**按你的赛道选读（三选一）：**

| 赛道                  | 教程导航                                                                                                                                                 |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 快应用 / 手表应用创新 | [快应用教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/quickapp/quickapp_guide_index.md)                         |
| AI 硬件产品创新       | [AI 硬件赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md)              |
| 新硬件适配            | [新硬件适配赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_guide_index.md) |

---

## 二、第一步：拉取完整工程

用组委会提供的命令一键拉取「openvela 全量源码 + 你的专属仓」：

```bash
repo init -u https://github.com/open-vela/contest2026_087_gaiduimingyizhanyongdui \
  -b dev-ai-contest-2026 -m contest2026_087_gaiduimingyizhanyongdui.xml
repo sync -c -j8
```

同步后，你的整个仓库位于工作区的 `contest2026_087_gaiduimingyizhanyongdui/`，openvela 全量源码在外层（`nuttx/`、`apps/`、`packages/`、`vendor/` 等）。

---

## 三、第二步：在哪里写代码

**只在自己的仓目录 `contest2026_087_gaiduimingyizhanyongdui/` 里开发。** 不同作品形态放在对应子目录，manifest 会通过 `<linkfile>` 把它们**软链**到 openvela 编译树该在的位置——你不用手动 copy：

| 作品形态 | 你的代码放这里             | 系统自动映射到                                 |
| -------- | -------------------------- | ---------------------------------------------- |
| 应用     | `app/hello_app/`           | `packages/demos/contest2026_087_hello_app`     |
| 快应用   | `quickapp/hello_quickapp/` | `packages/apps/contest2026_087_hello_quickapp` |
| 板级适配 | `board/contest_board/`     | `vendor/openvela/boards/contest2026_087_board` |

> 用不到的形态目录可以删掉；新增作品时按同样规则加子目录，并在 `contest2026_087_gaiduimingyizhanyongdui.xml` 里补一条 `<linkfile>` 映射即可。**生产仓库（packages/nuttx/vendor 等）零改动。**

建议仓库目录约定（便于评委定位）：

```text
app/ | quickapp/ | board/   # 你的作品代码
logs/                       # AI Coding 日志（主动导出后提交，格式见 logs/README.md）
README.md                   # 作品说明（提交前请改成你自己的，见第六节）
```

> 仓内附带了一个 `.gitignore.example`，给出了**编译产物**等不需要进仓的文件示例。如需启用，`cp .gitignore.example .gitignore` 后按需增删即可。**注意 `logs/` 下最终导出的 AI Coding 日志必须提交，不要忽略。**
>
> `logs/` 的目录结构与提交格式见 [logs/README.md](logs/README.md)。

---

## 四、第三步：编译与运行

编译/运行步骤随作品形态不同而不同，请参考你所在赛道的教程导航：

- 快应用 / 手表应用：[快应用教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/quickapp/quickapp_guide_index.md)（含模拟器与开发板部署）。
- AI 硬件产品创新：[AI 硬件赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md)（环境搭建、编译烧录、Skill 开发）。
- 新硬件适配：[新硬件适配赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_guide_index.md)（BSP 移植、最小 NSH 基线）。

子目录已通过 manifest 中的 `<linkfile>` 软链进 openvela 编译树，因此构建在 openvela 工作区**根目录**（即你这个仓的上一级）进行。openvela 使用 `build.sh` 作为统一入口，接收一个 **board config 路径**作为参数：

```bash
# 进入 openvela 工作区根目录（你的仓的上一级）
cd ..

# 通用语法：第一个参数是 board config 路径，第二个参数可以是 menuconfig / distclean 等
./build.sh <board-config-path> [menuconfig|distclean] [-j8]
```

> 具体的 board config 路径、目标产物、模拟器/真机部署方式请以你所在赛道的教程导航为准。本仓 `app/` `quickapp/` `board/` 三个示例骨架对应的 Kconfig 选项可通过 `menuconfig` 启用。

---

## 五、第四步：提交作品

1. **fork** 你的专属仓 → 开发 → `git commit` 并推送 → 向专属仓发起 **Pull Request**，可**自行 review 并合入**（无需等组委会）。
2. **AI Coding 日志**：与 AI 工具的对话会自动记录到本机 staging（不会自动上传），需你**主动导出/打包**选定会话到仓内 `logs/` 目录后一并提交。详见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
3. 若需改动 **nuttx 等公共仓库**，不在本仓改，而是 fork 对应公共仓、以 PR 提交到 `dev-ai-contest-2026` 分支，由组委会 review 后合入。

> ⏰ **提交作品截止：9 月 20 日**。截止后统一收回 push 权限，仍可查看 / clone。
>
> 获奖后再按要求将作品 PR 至 openvela 上游对应仓库（走标准 PR + CI 流程）。

### 关于 PR 与 CLA

- 本仓所有改动通过 **Pull Request** 合入（分支保护强制，可自行合入自己的 PR）。
- 首次贡献需在[**官网签署 CLA**](https://openvela.com/#/community/cla)；PR 上会自动跑 `cla/signature` 检查，在官网签署成功后，在 PR 评论 `/check-cla` 复检即可通过。

---

## 六、提交前：把本 README 改成你的作品说明

本文件目前是组委会给的**使用说明书**。**作品提交前，请把它替换成你自己作品的说明**，方便评委快速了解你做了什么、怎么跑起来。建议至少包含以下内容：

```markdown
# <你的作品名>

## 一、作品简介
<一句话/一段话说明这个作品是什么、解决什么问题、亮点在哪>

## 二、选题方向
<快应用 / 手表应用创新 ｜ AI 硬件产品创新 ｜ 新硬件适配 ｜ 自定方向，并简述理由>

## 三、目录结构
<列出你这个仓里各目录/文件的作用，例如：>
- `app/xxx/`        — <说明>
- `board/xxx/`      — <说明>
- `quickapp/xxx/`   — <说明>
- `logs/`           — AI Coding 日志
- `docs/` 或其他    — <说明>

## 四、运行方式
<拉取工程后，如何编译、烧录/部署、运行的完整步骤；最好能让评委照着一步步复现>

## 五、AI Coding 使用说明
<说明本作品如何借助 AI 辅助开发：
- 在需求拆解 / 方案设计 / 编码 / 调试 / 文档等环节如何与 AI 协作；
- AI 对开发效率或质量带来的实际帮助。
完整对话日志见 logs/ 目录>
```

> 提示：将会根据「作品本身 + 你的 README 说明 + `logs/` 里的 AI Coding 日志」来理解和评估你的作品，README 写清楚很重要。

---

## 附：仓库命名规范

`contest2026_<编号>_<队伍名>` — 编号三位零填充；队名 slug（全小写、英文/拼音、连字符）。例：`contest2026_087_gaiduimingyizhanyongdui`。
（仓库由组委会统一创建，**每队仅一个仓**，无需自行命名。）
