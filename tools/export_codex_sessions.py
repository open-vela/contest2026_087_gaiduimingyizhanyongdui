#!/usr/bin/env python3
"""把 Codex 会话补导成比赛要求的 logs JSONL。

Codex 的会话文件是 {timestamp, type, payload} 结构, 与组委会采集器
当前解析的 transcript 格式不同。本脚本按官方 event.schema.json 做字段
映射, 只搬运真实的对话与工具调用内容, 不改写文本, 不新增内容。

用法:
  python tools/export_codex_sessions.py --login <login> --team-id <id> --list
  python tools/export_codex_sessions.py --login <login> --team-id <id> --dry-run
  python tools/export_codex_sessions.py --login <login> --team-id <id> --confirm
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = "1.0"
TOOL = "codex"
# 脱敏规则: 前三条与组委会 contest-log-collector 的默认规则一致,
# 后两条覆盖 2026-07-12 会话里出现的 AWS 预签名链接。
REDACT_RULES = [
    (re.compile(r"sk-[A-Za-z0-9_-]{20,}"), "sk-***REDACTED***"),
    (re.compile(r"ghp_[A-Za-z0-9]{36}"), "ghp_***REDACTED***"),
    (re.compile(r"Bearer\s+[A-Za-z0-9._\-+/=]+"), "Bearer ***REDACTED***"),
    (re.compile(r"AKIA[0-9A-Z]{16}"), "AKIA***REDACTED***"),
    (re.compile(r"X-Amz-Signature=[0-9a-fA-F]+"),
     "X-Amz-Signature=***REDACTED***"),
]


def redact_value(value, rules, counter):
    """与组委会 snapshot_core 相同的脱敏逻辑, 递归处理字符串字段。"""
    if isinstance(value, str):
        out = value
        for pattern, replacement in rules:
            out, count = pattern.subn(replacement, out)
            counter[0] += count
        return out
    if isinstance(value, list):
        return [redact_value(item, rules, counter) for item in value]
    if isinstance(value, dict):
        return {k: redact_value(v, rules, counter) for k, v in value.items()}
    return value
KEYWORDS = (
    "contest2026_087",
    "gaiduimingyizhanyongdui",
    "FOCUS AIoT",
    "focus_aiot",
)
# Codex 会把内置指令、环境信息以及上下文压缩后的历史回放写成 user 事件,
# 这些不是人与 AI 的真实对话, 导出时跳过。
INJECTED_PREFIXES = (
    "The following is the Codex agent history",
    "<environment_context>",
    "<recommended_plugins>",
    "<skills_instructions>",
    "<plugins_instructions>",
    "<permissions instructions>",
    "# AGENTS.md instructions",
)


def is_injected(text: str) -> bool:
    return text.startswith(INJECTED_PREFIXES)


def norm_ts(value: str | None, fallback: str) -> str:
    if not value:
        return fallback
    try:
        dt = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return fallback
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    dt = dt.astimezone(timezone.utc)
    return dt.strftime("%Y-%m-%dT%H:%M:%S.") + f"{dt.microsecond // 1000:03d}Z"


def now_iso() -> str:
    dt = datetime.now(timezone.utc)
    return dt.strftime("%Y-%m-%dT%H:%M:%S.") + f"{dt.microsecond // 1000:03d}Z"


def local_date(ts: str) -> str:
    dt = datetime.fromisoformat(ts.replace("Z", "+00:00"))
    return dt.astimezone().strftime("%Y-%m-%d")


def maybe_json(value):
    if not isinstance(value, str):
        return value
    try:
        return json.loads(value)
    except (ValueError, TypeError):
        return value


def read_events(path: Path) -> list[dict]:
    events = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except ValueError:
                continue
    return events


def matches(path: Path) -> bool:
    """任一关键词出现即认为与比赛项目相关, 命中即可提前结束。"""
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if any(word in line for word in KEYWORDS):
                return True
    return False


def convert(raw_events: list[dict]) -> tuple[list[dict], dict]:
    """把 Codex rollout 事件映射成比赛事件, 返回事件列表与会话元数据。"""
    out: list[dict] = []
    meta = {"cwd": None, "model": None, "session_id": None}
    for raw in raw_events:
        payload = raw.get("payload") or {}
        top = raw.get("type")

        if top == "session_meta":
            meta["cwd"] = payload.get("cwd") or meta["cwd"]
            meta["session_id"] = payload.get("session_id") or meta["session_id"]
            continue
        if top == "turn_context":
            meta["model"] = payload.get("model") or meta["model"]
            continue
        if top != "response_item":
            continue

        kind = payload.get("type")
        ts = raw.get("timestamp")
        cwd = payload.get("cwd") or meta["cwd"]

        if kind == "message":
            role = str(payload.get("role") or "").lower()
            # developer / system 属于工具内置指令, 不计入对话记录。
            if role not in ("user", "assistant"):
                continue
            text = "\n".join(
                block.get("text", "")
                for block in (payload.get("content") or [])
                if isinstance(block, dict) and block.get("text")
            ).strip("\n")
            if not text:
                continue
            if is_injected(text):
                continue
            event = {"ts": ts, "role": role, "text": text}
            if role == "assistant" and meta["model"]:
                event["model"] = meta["model"]
            if cwd:
                event["cwd"] = cwd
            out.append(event)

        elif kind == "reasoning":
            chunks = []
            for key in ("summary", "content"):
                for block in (payload.get(key) or []):
                    if isinstance(block, dict) and block.get("text"):
                        chunks.append(block["text"])
            if chunks:
                event = {"ts": ts, "role": "assistant",
                         "thinking": "\n".join(chunks)}
                if cwd:
                    event["cwd"] = cwd
                out.append(event)

        elif kind in ("custom_tool_call", "function_call"):
            arguments = payload.get("input") if kind == "custom_tool_call" \
                else payload.get("arguments")
            out.append({
                "ts": ts,
                "role": "tool",
                "tool_name": payload.get("name") or kind,
                "tool_call_id": payload.get("call_id") or payload.get("id") or "",
                "input": maybe_json(arguments),
                "output": None,
                **({"cwd": cwd} if cwd else {}),
            })

        elif kind in ("custom_tool_call_output", "function_call_output"):
            out.append({
                "ts": ts,
                "role": "tool",
                "tool_name": "<result>",
                "tool_call_id": payload.get("call_id") or payload.get("id") or "",
                "input": None,
                "output": payload.get("output"),
                **({"cwd": cwd} if cwd else {}),
            })

    clean: list[dict] = []
    fallback = now_iso()
    for event in out:
        event["ts"] = norm_ts(event.get("ts"), fallback)
        clean.append(event)
    return clean, meta


def build_jsonl(events: list[dict], session_id: str, team_id: str,
                login: str) -> tuple[str, int]:
    lines = []
    redacted_total = 0
    for seq, event in enumerate(events):
        counter = [0]
        payload = redact_value(event, REDACT_RULES, counter)
        redacted_total += counter[0]
        record = {
            "schema_version": SCHEMA_VERSION,
            "session_id": session_id,
            "team_id": team_id,
            "github_login": login,
            "tool": TOOL,
            "seq": seq,
            **payload,
        }
        if counter[0]:
            record["redacted_count"] = counter[0]
        lines.append(json.dumps(record, ensure_ascii=False))
    return "\n".join(lines) + ("\n" if lines else ""), redacted_total


def find_sessions(sessions_dir: Path) -> list[Path]:
    found = []
    for path in sorted(sessions_dir.rglob("*.jsonl")):
        try:
            if matches(path):
                found.append(path)
        except OSError:
            continue
    return found


def session_id_of(raw_events: list[dict]) -> str | None:
    for raw in raw_events:
        if raw.get("type") == "session_meta":
            sid = (raw.get("payload") or {}).get("session_id")
            if sid:
                return sid
    return None


def fingerprint(event: dict) -> str:
    keys = ("role", "text", "thinking", "tool_name", "tool_call_id",
            "input", "output")
    return json.dumps({k: event.get(k) for k in keys},
                      sort_keys=True, ensure_ascii=False)


def merge_segments(segments: list[list[dict]]) -> tuple[list[dict], dict]:
    """同一个 session 被 fork 成多个 rollout 时, 合并各段并去重。

    fork 会把父会话的历史事件复制到新文件, 因此先按时间排序各段,
    再按内容指纹去掉重复事件, 保持原有先后顺序。
    """
    ordered = sorted(segments, key=lambda events: (
        events[0].get("timestamp", "") if events else ""))
    merged: list[dict] = []
    seen: set[str] = set()
    meta: dict = {"cwd": None, "model": None, "session_id": None}
    for raw_events in ordered:
        events, part_meta = convert(raw_events)
        for key, value in part_meta.items():
            if value and not meta.get(key):
                meta[key] = value
        for event in events:
            mark = fingerprint(event)
            if mark in seen:
                continue
            seen.add(mark)
            merged.append(event)
    merged.sort(key=lambda event: event.get("ts", ""))
    return merged, meta


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sessions-dir",
                        default=str(Path.home() / ".codex" / "sessions"))
    parser.add_argument("--dest", default=".")
    parser.add_argument("--login", required=True)
    parser.add_argument("--team-id", required=True)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--confirm", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--session", action="append", default=[],
                        help="只导出指定 session id, 可重复")
    args = parser.parse_args()

    sessions_dir = Path(args.sessions_dir).expanduser()
    dest = Path(args.dest).expanduser().resolve()
    files = find_sessions(sessions_dir)
    if args.list:
        for path in files:
            print(path)
        print(f"total={len(files)}")
        return 0

    groups: dict[str, list[list[dict]]] = {}
    for path in files:
        raw_events = read_events(path)
        sid = session_id_of(raw_events) or path.stem.split("-", 2)[-1]
        groups.setdefault(sid, []).append(raw_events)

    manifest_sessions = []
    written_total = 0
    bytes_total = 0
    for session_id, segments in sorted(groups.items()):
        if args.session and session_id not in args.session:
            continue
        events, meta = merge_segments(segments)
        if not events:
            print(f"skip (no convertible event): {session_id}")
            continue
        body, redacted = build_jsonl(events, session_id, args.team_id, args.login)
        size = len(body.encode("utf-8"))
        date = local_date(events[0]["ts"])
        rel = Path("logs") / args.login / date / f"{TOOL}__{session_id}.jsonl"
        print(f"{date}  events={len(events):5d}  {size / 1024:9.1f}KB  "
              f"segments={len(segments)}  redacted={redacted}  {session_id}")
        bytes_total += size
        if args.confirm:
            target = dest / rel
            if target.exists() and not args.force:
                print(f"  exists, skipped: {rel}")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(body, encoding="utf-8")
            written_total += 1
        manifest_sessions.append({
            "session_id": session_id,
            "tool": TOOL,
            "started_at": events[0]["ts"],
            "last_event_at": events[-1]["ts"],
            "event_count": len(events),
            "raw_event_count": sum(len(s) for s in segments),
            "source_segments": len(segments),
            "file_path": rel.as_posix(),
            "collection_mode": "cli",
            "health": "ok",
            "backfill_note": "codex rollout mapped by codex-backfill.py",
            **({"model": meta["model"]} if meta["model"] else {}),
        })

    print(f"sessions={len(manifest_sessions)} bytes={bytes_total / 1024 / 1024:.1f}MB "
          f"written={written_total}")
    if not args.confirm:
        print("dry-run: no file written (add --confirm to export)")
        return 0

    member_dir = dest / "logs" / args.login
    manifest_path = member_dir / "manifest.json"
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "team_id": args.team_id,
        "github_login": args.login,
        "generator": "codex-backfill@1.0.0",
        "updated_at": now_iso(),
        "sessions": manifest_sessions,
    }
    if manifest_path.exists():
        try:
            old = json.loads(manifest_path.read_text(encoding="utf-8"))
            # 本次导出的条目覆盖同名旧条目, 其余旧条目保留。
            merged = {s.get("session_id"): s for s in old.get("sessions", [])}
            for entry in manifest_sessions:
                merged[entry["session_id"]] = entry
            manifest["sessions"] = list(merged.values())
        except ValueError:
            pass
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest -> {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
