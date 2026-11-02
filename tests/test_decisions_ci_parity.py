"""The decisions CI job runs the lane contract's command and nothing else.

.loom/validation.conf checks a lane change with `bash scripts/check-locally.sh
decisions`. When check.decisions.yml repeated each command inline, the two
drifted: a step CI ran that the contract did not failed only in CI. So every
run step in the workflow must be either a system package install (runner
setup) or exactly that one command, and the command must appear once.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/check.decisions.yml"
CONTRACT = ROOT / ".loom/validation.conf"
ENTRY = "bash scripts/check-locally.sh decisions"
INSTALL = re.compile(r"^(sudo\s+)?apt-get\s+(update|install)\b")
RUN = re.compile(r"^(\s*)(?:-\s+)?run:\s*(.*)$")


def run_commands(text):
    """Every run: value in the workflow, block scalars joined into one string."""
    lines = text.splitlines()
    commands = []
    index = 0
    while index < len(lines):
        match = RUN.match(lines[index])
        index += 1
        if not match:
            continue
        value = match.group(2).strip()
        if value not in ("|", ">", "|-", ">-"):
            commands.append(value)
            continue
        base = len(lines[index - 1]) - len(lines[index - 1].lstrip())
        block = []
        while index < len(lines):
            line = lines[index]
            if line.strip() and len(line) - len(line.lstrip()) <= base:
                break
            if line.strip():
                block.append(line.strip())
            index += 1
        commands.append("\n".join(block))
    return commands


def is_install(command):
    return all(INSTALL.match(part.strip()) for part in command.split("&&"))


class DecisionsCiParityTest(unittest.TestCase):
    def test_every_run_step_is_an_install_or_the_contract_command(self):
        commands = run_commands(WORKFLOW.read_text())
        extra = [c for c in commands if c != ENTRY and not is_install(c)]
        self.assertEqual([], extra, "run these from run_decisions in scripts/check-locally.sh instead")
        self.assertEqual(1, commands.count(ENTRY), commands)

    def test_contract_check_is_the_ci_command(self):
        checks = [
            line.split(":", 1)[1].strip()
            for line in CONTRACT.read_text().splitlines()
            if line.startswith("check:")
        ]
        self.assertEqual([ENTRY], checks)

    def test_parser_reads_block_and_inline_steps(self):
        sample = (
            "    steps:\n"
            "      - run: sudo apt-get update && sudo apt-get install -y gcc\n"
            "      - name: x\n"
            "        run: |\n"
            "          set -e\n"
            "          make\n"
            "      - run: bash scripts/check-locally.sh decisions\n"
        )
        commands = run_commands(sample)
        self.assertEqual(
            [
                "sudo apt-get update && sudo apt-get install -y gcc",
                "set -e\nmake",
                ENTRY,
            ],
            commands,
        )
        self.assertTrue(is_install(commands[0]))
        self.assertFalse(is_install(commands[1]))


if __name__ == "__main__":
    unittest.main()
