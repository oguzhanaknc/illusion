#!/usr/bin/env python3
"""Turn an `illusion_mcp --record` session into a replayable transcript (ADR 0038, plan §9).

    python tools/acceptance/normalize_transcript.py session.jsonl tests/acceptance/tetris/recorded.jsonl

Every successful tool call becomes a step `{"call": <command>, "params": ...}` of the editor protocol, in the
order the agent made it:

- Failed calls (JSON-RPC errors and tool results with isError) are left out: they changed nothing.
- Discovery calls that only read (guide, describe, schemas, file reads, scene trees) are left out.
- Entity ids are replaced by captures: the step whose result first held an id captures it as `${name}`, and
  later params refer to the capture, so the replay works whatever ids the editor hands out.
- `project.file_write` texts go to `files/<mount>/<path>` next to the transcript and the step reads them
  back with `{"$file": ...}`, so reviewers read real sources and the build can compile them.
- `component.get` and `scene.query` steps expect the values and totals the agent saw, except while a play
  session runs unpaused: its state then depends on how many frames passed between two calls.
- Timestamps, `_editor` metadata and text summaries are dropped.

At the end the script appends the steps the acceptance checks need (the C4 captures, a restart and a
module build that loads the agent's modules again); `--no-epilogue` leaves them out.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
ID = re.compile(r"^[0-9a-f]{16}$")

# Read-only calls an agent makes to find its way; they are not part of what it built.
DISCOVERY = {
    "editor.capabilities", "editor.list_commands", "editor.describe", "editor.help", "component.schema",
    "component.types", "project.file_read", "project.file_stat", "project.info", "scene.tree", "asset.list",
    "selection.get", "ui.state",
}


def command_names() -> dict[str, str]:
    catalog = json.loads((ROOT / "editor/tests/data/commands/catalog.json").read_text(encoding="utf-8"))
    return {name.replace(".", "_"): name for name in catalog}


def strip_editor(value):
    if isinstance(value, dict):
        return {k: strip_editor(v) for k, v in value.items() if k != "_editor"}
    if isinstance(value, list):
        return [strip_editor(v) for v in value]
    return value


def find_ids(value, pointer=""):
    """(pointer, id, name hint) of every entity id in a result."""
    if isinstance(value, dict):
        hint = value.get("path") or value.get("name") or ""
        for key, item in value.items():
            if key == "id" and isinstance(item, str) and ID.match(item):
                yield pointer + "/id", item, hint
            else:
                yield from find_ids(item, f"{pointer}/{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from find_ids(item, f"{pointer}/{index}")


def variable_name(hint: str, taken: set[str]) -> str:
    base = re.sub(r"[^a-z0-9]+", "_", hint.rsplit("/", 1)[-1].lower()).strip("_")
    base = base if base and not ID.match(base.lstrip("#")) and not base[0].isdigit() else "entity"
    name, n = base, 2
    while name in taken:
        name, n = f"{base}{n}", n + 1
    taken.add(name)
    return name


def replace_ids(value, ids: dict[str, str]):
    if isinstance(value, str):
        return "${" + ids[value] + "}" if value in ids else value
    if isinstance(value, dict):
        return {k: replace_ids(v, ids) for k, v in value.items()}
    if isinstance(value, list):
        return [replace_ids(v, ids) for v in value]
    return value


def file_name(path: str) -> str:
    mount, _, rest = path.partition(":/")
    if not mount or not rest or ".." in rest.split("/"):
        sys.exit(f"cannot store the file {path!r}")
    return f"files/{mount}/{rest}"


def epilogue() -> list[dict]:
    return [
        {"comment": "Appended by normalize_transcript.py for checks.jsonl: the C4 captures, a restart, and a "
                    "module build that loads the agent's module again in the restarted editor."},
        {"call": "entity.get", "params": {"entity": "/Board"}, "capture": {"board": "/entity/id"}},
        {"call": "scene.info", "capture": {"hash": "/hash"}, "expect": {"result": {"dirty": False}}},
        {"call": "project.file_read", "params": {"path": "assets:/scenes/main.scene"},
         "capture": {"sceneText": "/text"}},
        {"restart": {"arguments": ["--project", "${root}"]}},
        {"call": "module.build", "expect": {"result": {"succeeded": True, "errors": 0}}},
    ]


def normalize(record: Path, output: Path, with_epilogue: bool) -> tuple[int, int]:
    commands = command_names()
    requests, responses = [], {}
    for line in record.read_text(encoding="utf-8").splitlines():
        entry = json.loads(line)
        message = json.loads(entry["line"])
        if entry["dir"] == "in" and message.get("method") == "tools/call":
            requests.append(message)
        elif entry["dir"] == "out" and "id" in message and "method" not in message:
            responses[json.dumps(message["id"])] = message

    steps = [{"comment": f"Recorded agent session ({record.name}), normalised by "
                         "tools/acceptance/normalize_transcript.py; checks.jsonl verifies the result."}]
    ids: dict[str, str] = {}
    taken: set[str] = set()
    dropped = 0
    realtime = False  # a play session runs without being paused
    for request in requests:
        reply = responses.get(json.dumps(request["id"]))
        result = reply.get("result") if reply else None
        if result is None or result.get("isError"):
            dropped += 1
            continue
        tool = request["params"]["name"]
        arguments = request["params"].get("arguments") or {}
        if tool == "illusion_call":
            command, arguments = arguments.get("method", ""), arguments.get("params") or {}
        else:
            command = commands.get(tool, "")
        if not command:
            sys.exit(f"unknown tool {tool!r}")
        if command in DISCOVERY:
            dropped += 1
            continue
        structured = strip_editor(result.get("structuredContent") or {})
        step: dict = {"call": command}
        params = replace_ids(arguments, ids)
        if command == "project.file_write" and isinstance(params.get("text"), str):
            name = file_name(params["path"])
            target = output.parent / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(params["text"], encoding="utf-8", newline="\n")
            params = dict(params, text={"$file": name})
        if params:
            step["params"] = params
        if command == "project.create":
            step["capture"] = {"root": "/root"}
            taken.add("root")
        captures = {}
        for pointer, value, hint in find_ids(structured):
            if value not in ids:
                ids[value] = variable_name(hint, taken)
                captures[ids[value]] = pointer
        if captures:
            step.setdefault("capture", {}).update(captures)
        if command == "play.start":
            realtime = True
        elif command == "play.pause":
            realtime = not arguments.get("paused", True)
        elif command == "play.stop":
            realtime = False
        if realtime:
            pass
        elif command == "component.get" and "value" in structured:
            step["expect"] = {"result": {"value": structured["value"]}}
        elif command == "scene.query" and "total" in structured:
            step["expect"] = {"result": {"total": structured["total"]}}
        elif command == "module.build":
            step["expect"] = {"result": {"succeeded": structured.get("succeeded"), "errors": structured.get("errors")}}
        steps.append(step)
    if with_epilogue:
        steps.extend(epilogue())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(json.dumps(s, ensure_ascii=False) + "\n" for s in steps), encoding="utf-8",
                      newline="\n")
    return len(steps), dropped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("record", type=Path, help="the --record file of illusion_mcp")
    parser.add_argument("output", type=Path, help="the transcript to write (files/ goes next to it)")
    parser.add_argument("--no-epilogue", action="store_true", help="do not append the acceptance steps")
    args = parser.parse_args()
    written, dropped = normalize(args.record, args.output, not args.no_epilogue)
    print(f"{args.output}: {written} steps ({dropped} failed or discovery calls left out)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
