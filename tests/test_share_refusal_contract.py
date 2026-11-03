"""The share verb's refusal contract: the words the bridge reads back.

The outside system that writes `share` commands matches these exact detail
strings and the `retryable` flag to decide whether to ask again. A rename on
this side silently breaks that read, so the strings are pinned here against
the adapter's own source. Run from any directory; the path is taken from
this file's location. tests/test_share_refusal_contract_source.cpp runs this
in the same loop as every other test.
"""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ShareRefusalContractTest(unittest.TestCase):
    def test_the_refusal_words_and_flag_are_unchanged(self):
        source = (ROOT / "src/mod_overseer.cpp").read_text()
        start = source.index("static char const* DoShare")
        end = source.index("// kind='quest': take a quest", start)
        share = source[start:end]
        for fragment in (
            "retryable",
            '"holder does not carry the quest"',
            '"quest is not sharable"',
            '"taker offline"',
            '(retryable ? "true" : "false")',
            "bool retryable = true",
        ):
            self.assertIn(fragment, share)


if __name__ == "__main__":
    unittest.main()
