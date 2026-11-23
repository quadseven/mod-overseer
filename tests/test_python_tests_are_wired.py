"""Every Python test under tests/ is run by the decisions check.

check.decisions.yml runs only `scripts/check-locally.sh decisions`, and that
function lists its Python tests one by one. A test file added without a line
there passes once and then never runs again. This guard fails for each
tests/test_*.py or tests/replay_*.py whose file name is named nowhere in the
decisions function or in a workflow file, unless it is in ALLOWLIST with a
reason, so a gap stays visible and counted.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
SCRIPT = ROOT / "scripts/check-locally.sh"
WORKFLOWS = ROOT / ".github/workflows"
TEST_NAME = re.compile(r"^(test|replay)_.*\.py$")

# file name -> one-line reason it is not run by the decisions check.
ALLOWLIST = {
}


def decisions_function(script_text):
    """The body of run_decisions() in check-locally.sh."""
    match = re.search(r"^run_decisions\(\) \{\n(.*?)^\}", script_text, re.S | re.M)
    if not match:
        raise AssertionError("run_decisions() not found in scripts/check-locally.sh")
    return match.group(1)


def unwired(test_names, run_text, allowlist):
    """Test file names that appear nowhere in run_text and are not allowlisted."""
    return sorted(
        name
        for name in test_names
        if name not in allowlist and name not in run_text
    )


def test_files(directory):
    return sorted(p.name for p in directory.glob("*.py") if TEST_NAME.match(p.name))


def run_text_from_repo():
    text = decisions_function(SCRIPT.read_text())
    for workflow in sorted(WORKFLOWS.glob("*.y*ml")):
        text += "\n" + workflow.read_text()
    return text


class PythonTestsAreWiredTest(unittest.TestCase):
    def test_every_python_test_is_run_or_allowlisted(self):
        missing = unwired(test_files(TESTS), run_text_from_repo(), ALLOWLIST)
        self.assertEqual(
            [], missing,
            "not run by scripts/check-locally.sh decisions or a workflow; "
            "wire each or add it to ALLOWLIST with a reason: %s" % ", ".join(missing),
        )

    def test_a_new_unwired_replay_is_caught(self):
        names = ["replay_new_thing.py", "replay_door_floor.py"]
        run_text = "python3 -B tests/replay_door_floor.py"
        self.assertEqual(["replay_new_thing.py"], unwired(names, run_text, {}))

    def test_an_allowlisted_file_is_not_reported(self):
        names = ["replay_new_thing.py"]
        self.assertEqual([], unwired(names, "", {"replay_new_thing.py": "reason"}))

    def test_helper_modules_are_not_tests(self):
        self.assertIsNone(TEST_NAME.match("helper.py"))
        self.assertIsNotNone(TEST_NAME.match("replay_x.py"))
        self.assertIsNotNone(TEST_NAME.match("test_x.py"))

    def test_allowlist_entries_have_reasons_and_exist(self):
        existing = set(test_files(TESTS))
        for name, reason in ALLOWLIST.items():
            self.assertTrue(reason.strip(), name)
            self.assertIn(name, existing)


if __name__ == "__main__":
    unittest.main()
