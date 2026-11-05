"""The adapter CI job runs its compile command as an argv, never through eval.

check.adapter.yml reads the adapter's compile command out of
compile_commands.json, which cmake writes from a tree that includes the pull
request's own CMakeLists.txt. Handing that string to eval lets a flag value
such as -DX=$(cmd) run as shell. scripts/check-locally.sh splits the same
string with xargs into an argv instead (#1346). This test runs the workflow's
own lines on a command carrying a $(...) payload and fails if the payload
executes.
"""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/check.adapter.yml"
STEP = "- name: Compile src/mod_overseer.cpp, and only that"


def compile_lines(text):
    """The lines that run the compile: after `started=$SECONDS`, up to `|| status=$?`."""
    lines = text.splitlines()
    start = next(i for i, line in enumerate(lines) if line.strip() == STEP)
    begin = next(i for i in range(start, len(lines)) if lines[i].strip() == "started=$SECONDS")
    end = next(i for i in range(begin, len(lines)) if lines[i].rstrip().endswith("|| status=$?"))
    return [line.strip() for line in lines[begin + 1 : end + 1]]


class AdapterCiArgvTest(unittest.TestCase):
    def test_step_does_not_eval(self):
        body = compile_lines(WORKFLOW.read_text())
        code = [line for line in body if line and not line.startswith("#")]
        self.assertFalse([line for line in code if line.split()[0] == "eval"], code)
        self.assertIn('mapfile -t args < <(xargs -n1 <<<"$syntax")', code)
        self.assertEqual("status=0", code[-2])

    def test_payload_in_a_flag_does_not_run(self):
        body = "\n".join(compile_lines(WORKFLOW.read_text()))
        with tempfile.TemporaryDirectory() as tmp:
            marker = os.path.join(tmp, "ran")
            script = "\n".join(
                [
                    'syntax="true -DINJECT=\\$(touch ' + marker + ')"',
                    'src=/dev/null',
                    body,
                    'echo "status=$status"',
                ]
            )
            result = subprocess.run(
                ["bash", "-e", "-c", script], capture_output=True, text=True, timeout=30
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("status=0", result.stdout)
            self.assertFalse(os.path.exists(marker), "the compile command's $(...) ran as shell")

    def test_a_failing_compile_sets_status(self):
        body = "\n".join(compile_lines(WORKFLOW.read_text()))
        script = "\n".join(['syntax="false"', "src=/dev/null", body, 'echo "status=$status"'])
        result = subprocess.run(
            ["bash", "-e", "-c", script], capture_output=True, text=True, timeout=30
        )
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("status=1", result.stdout)


if __name__ == "__main__":
    unittest.main()
