# Copilot 自定义 Agent（reSpeaker）配置说明

目标：让你在 `C:\Users\seeed\AppData\Roaming\Code\User\prompts\respeaker_agent.agent.md` 的自定义 Agent 能稳定做固件开发，并通过“写入工作区文件”实现跨会话持续记忆。

## 1) 持续记忆如何实现
Copilot/Agent 的模型本身**不会**把你贴过的 PRD、网页等内容自动长期保存。

工程化做法：把“解读结论/关键决策/未决问题”写进仓库文件，作为可版本控制的记忆载体。

本工程已提供：
- `AGENT_MEMORY.md`：长期记忆（持续更新）
- `docs/requirements_interpretation.md`：需求书解读（固件/SDK 视角）

你的自定义 agent 已被修改为：每次任务开始必须先读这些文件，并在确认新信息后写回。

## 2) Tool 能力如何配置
在自定义 agent 的 frontmatter 里通过 `tools:` 开启。

当前 `respeaker_agent.agent.md` 已启用：
- `Read/Grep/Glob`：读文件、搜代码
- `Edit/Write`：直接修改代码/写文件
- `Bash`：跑命令（`python`、`west`、`git`、脚本等；Windows 也可在 Bash 里调用 `powershell -NoProfile -Command ...`）
- `Fetch`：解读网页（若遇到 403/挑战页，改为让用户粘贴正文/截图）

如果你发现 `Fetch` 不生效：通常是 VS Code/Copilot 版本或策略限制，此时用“复制网页正文/截图”的方式仍可完成解读。

## 3) MCP 服务如何配置（说明与建议）
说明：我（当前对话里的 Copilot）能调用终端/读写文件/网页抓取，是因为运行在一个带工具的环境里；这并不等同于你本地 agent 已经自动配置好了 MCP。

如果你希望在 VS Code 里启用 MCP servers（例如 memory、外部检索、专用工具链等），一般需要：
1. 打开 VS Code 命令面板（Ctrl+Shift+P），搜索 `MCP` 相关命令（不同版本名字可能略有差异，如 Configure/Add Servers）。
2. 按提示添加 server（通常是一个命令 + 参数，或一个 json 配置）。

建议优先级：
- **首先**用 `AGENT_MEMORY.md` 做“确定可用”的持久记忆（不依赖任何插件/网络）。
- **然后**再考虑 MCP memory server（当你需要跨多个 repo/工作区共享记忆，或需要结构化存储/检索时）。

## 4) reSpeaker 固件开发推荐工作流
- 任何新功能先落在状态机约束中（待机/录音/传输/升级/故障），避免 OTA/录音/传输互相打架。
- Wi‑Fi 默认按需开启；录音期间尽量不跑大吞吐传输。
- 每次确认一项关键决策（Wi‑Fi 拓扑、FS、OTA 方案、协议版本）就写回 `AGENT_MEMORY.md`。
