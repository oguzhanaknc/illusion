"""Self-test of illusion_rpc.py against a real editor (CTest editor.python_client).

    python tools/editor/test_illusion_rpc.py <path to illusion_editor>
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from illusion_rpc import EditorClient, RpcError  # noqa: E402


def main() -> int:
    editor_path = sys.argv[1]
    workspace = tempfile.mkdtemp(prefix="illusion-python-")
    try:
        with EditorClient.launch(editor_path, ["--deterministic", "--workspace", workspace]) as editor:
            project = editor.call("project.create", directory="game", name="Game")
            assert project["name"] == "Game", project
            editor.call("editor.subscribe", topics=["scene.changed"], mode="pull")
            board = editor.call("entity.create", {"name": "Board"})["entity"]
            assert board["path"] == "/Board", board
            assert editor.call("scene.info")["entities"] == 1
            try:
                editor.call("entity.get", entity="/Nope")
                raise AssertionError("entity.get of a missing entity succeeded")
            except RpcError as error:
                assert error.code == 1200, error
                assert error.data["kind"] == "not_found", error.data
            try:
                editor.call("entity.craete")
                raise AssertionError("an unknown method succeeded")
            except RpcError as error:
                assert error.code == -32601, error
                assert "entity.create" in error.data["suggestions"], error.data
            # Replies can arrive in the same frame; events are published only at endFrame().
            editor.call("editor.wait", frames=1)
            events = editor.call("editor.poll_events")
            assert any(event["topic"] == "scene.changed" for event in events["events"]), events
        assert os.path.isfile(os.path.join(workspace, "game", "project.illusion"))
    finally:
        shutil.rmtree(workspace, ignore_errors=True)
    print("illusion_rpc.py: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
