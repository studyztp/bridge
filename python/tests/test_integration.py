import subprocess
import sys
import pathlib
import time


def test_helper_integration_runs():
    base = pathlib.Path(__file__).resolve().parent.parent / "examples"
    helper = str(base / "helper_server.py")
    proc = subprocess.Popen([sys.executable, helper])
    try:
        # wait for completion (the example should exit on success)
        rc = proc.wait(timeout=10)
    finally:
        if proc.poll() is None:
            proc.kill()
    assert rc == 0
