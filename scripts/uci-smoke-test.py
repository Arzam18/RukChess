#!/usr/bin/env python3
"""Run a small UCI handshake/search against a built RukChess executable."""
import queue
import subprocess
import sys
import threading
import time


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} PATH_TO_RUKCHESS", file=sys.stderr)
        return 2

    executable = sys.argv[1]
    process = subprocess.Popen(
        [executable],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    assert process.stdin is not None and process.stdout is not None
    output: queue.Queue[str | None] = queue.Queue()

    def reader() -> None:
        try:
            for line in process.stdout:
                output.put(line.rstrip("\r\n"))
        finally:
            output.put(None)

    threading.Thread(target=reader, daemon=True).start()

    def send(line: str) -> None:
        process.stdin.write(line + "\n")
        process.stdin.flush()

    def wait_for(predicate, timeout: float, description: str) -> list[str]:
        collected: list[str] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = output.get(timeout=min(0.25, max(0.01, deadline - time.monotonic())))
            except queue.Empty:
                continue
            if line is None:
                break
            collected.append(line)
            print(line)
            if predicate(line):
                return collected
        raise RuntimeError(f"Timed out waiting for {description}; last output: {collected[-20:]}")

    try:
        # Send the mode selector alone: RukChess changes stdin buffering when
        # entering UCI mode, so do not pre-buffer subsequent commands.
        send("uci")
        handshake = wait_for(lambda line: line.strip() == "uciok", 20, "uciok")
        if not any("Load network...DONE (embedded;" in line for line in handshake):
            raise RuntimeError("engine did not report successful embedded NNUE loading")

        send("setoption name Hash value 16")
        send("isready")
        wait_for(lambda line: line.strip() == "readyok", 10, "readyok")
        send("position startpos")
        send("go depth 4")
        wait_for(lambda line: line.startswith("bestmove "), 25, "bestmove")
        send("quit")
        process.wait(timeout=5)
        print("RukChess UCI smoke test: PASS")
        return 0
    except Exception as exc:
        print(f"RukChess UCI smoke test: FAIL: {exc}", file=sys.stderr)
        process.kill()
        process.wait()
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
