from pathlib import Path


source = Path("src/mod_overseer.cpp").read_text()
share = source[source.index("static char const* DoShare"):source.index("// kind='quest': take a quest")]

assert 'retryable' in share
assert '"holder does not carry the quest"' in share
assert '"quest is not sharable"' in share
assert '"taker offline"' in share
assert '(retryable ? "true" : "false")' in share
assert 'bool retryable = true' in share
