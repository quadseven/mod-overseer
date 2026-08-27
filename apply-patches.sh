#!/usr/bin/env bash
#
# Apply this repo's local patches to the pinned upstream trees.
#
# WHY THIS EXISTS: UPSTREAM-PINS.env pins five upstream repositories by SHA and
# the build compiles them verbatim. That is the right default - but it left no
# way at all to carry a fix for a bug that lives in UPSTREAM's code. The only
# options were "open a PR upstream and wait" or "live with it". This is the
# third option: a small, auditable, exactly-matching patch applied at build
# time, on top of the pinned tree.
#
# WHY `git apply` AND NOT `patch -p1`:
#   - ZERO FUZZ. GNU patch will happily relocate a hunk and apply it at an
#     offset with reduced context ("Hunk #1 succeeded at 5561 with fuzz 2").
#     That is the failure mode this whole pipeline exists to prevent: a patch
#     that lands somewhere subtly wrong is worse than one that does not land.
#     `git apply` has no fuzz factor - the context matches exactly or it fails.
#   - WHITESPACE IS NOT NEGOTIABLE. `--whitespace=error` makes a patch that
#     would introduce trailing whitespace or a space/tab mix fail instead of
#     being quietly "fixed", so what lands is byte-for-byte what was reviewed.
#     `core.autocrlf=false` is forced for the same reason: a runner with CRLF
#     conversion on must not silently rewrite the tree under the patch.
#
# WHERE THIS SCRIPT PREVIOUSLY LIED TO US (infra#2849). The two bullets above
# used to be followed by a third, claiming `git apply` has "HONEST EXIT CODES"
# and is "all-or-nothing per invocation, exits non-zero on any rejected hunk".
# That is TRUE for a rejected hunk and FALSE for a SKIPPED file, and the
# difference ran three patches into the ground for days. A confident wrong
# guarantee sitting exactly where the defence should have been written is why
# nobody defended against skips, so it is corrected here rather than deleted:
#
#   `git apply` resolves the paths in a patch against the top level of the
#   REPOSITORY IT DISCOVERS, not against the directory passed to `-C`. The
#   build deletes each module's `.git` (build.azerothcore-playerbots.yml:150),
#   so from $ACORE_ROOT/modules/mod-playerbots git walks UP, finds the CORE's
#   repository, and gives itself a prefix of `modules/mod-playerbots/`. Any
#   file in the patch that falls outside that prefix is SKIPPED - printed as
#   `Skipped patch 'src/...'.` on STDERR, and then exit 0. Not rejected.
#   Skipped. `git apply --check` exits 0 on it too.
#
#   Which of a patch's paths fall outside the prefix depends on the patch's
#   HEADER FORMAT, which is why this hit some patches and not others. A patch
#   carrying `diff --git a/... b/...` is flagged toplevel-relative and its
#   paths are used as-is (`src/...`) - outside the prefix, skipped, silently.
#   A patch with only `---`/`+++` is treated as relative to the current
#   directory and git PREPENDS the prefix (`modules/mod-playerbots/src/...`) -
#   which lands on the right file, but only by the accident of the module
#   sitting at exactly that path inside the core repo.
#
# So the three defences below are not belt-and-braces decoration. They are the
# reason this script can be believed at all:
#
#   1. RESOLVE AGAINST THE TREE WE WERE GIVEN. Every `git apply` runs with
#      GIT_CEILING_DIRECTORIES set to the target's parent, so git cannot walk
#      up into an enclosing repository. The target becomes the top level (or
#      no repository is found at all, which has the same effect), the prefix
#      is empty, and BOTH header formats resolve to the same files.
#   2. ASSERT THAT WORKED. The prefix is measured with `rev-parse
#      --show-prefix` and a non-empty one is a hard failure, so if the ceiling
#      is ever defeated we stop instead of skipping.
#   3. A SKIP IS A FAILURE, AND SO IS AN APPLY THAT CHANGED NOTHING. `git
#      apply` output is captured and any `Skipped patch` announcement fails
#      the build. Then, independently of anything git says, every file the
#      patch names is hashed before and after and MUST have changed. Defence 3
#      is the one that does not depend on knowing this bug: a patch reported
#      applied has demonstrably modified bytes, or the build stops.
#
# THE POINT OF ALL THAT: when upstream merges the same fix and we bump the pin,
# the patch STOPS APPLYING and this script FAILS THE BUILD. That is deliberate.
# A patch that quietly stops doing anything is indistinguishable from a patch
# that is still working, and the human never finds out the fix is now doubled,
# dropped, or half-applied. Loud failure forces the one decision that matters:
# delete the patch, or rebase it.
#
# LAYOUT: patches/<upstream-tree-name>/NNNN-*.patch , where <upstream-tree-name>
# is exactly the directory the build clones that repo into:
#   patches/azerothcore-wotlk/   -> the core fork itself   ($ACORE_ROOT)
#   patches/mod-playerbots/      -> $ACORE_ROOT/modules/mod-playerbots
#   patches/<any-other-module>/  -> $ACORE_ROOT/modules/<any-other-module>
# Adding a patch is therefore only ever "drop a file in the right directory".
# No workflow edit, no list to keep in sync here.
#
# USAGE:
#   apply-patches.sh <acore-root> [--check]
#
#   --check dry-runs every patch (`git apply --check`) and changes nothing.
#           NOTE that --check is strictly weaker than a real apply: it proves
#           a patch still matches, and it detects a skip, but it cannot prove
#           bytes changed, because by definition it changes none.
#           verify-patches.sh therefore runs a REAL apply against a throwaway
#           tree rather than using this mode - see the comment there.
#
# TESTS: tests/test-apply-patches.sh (bash, no network, no Docker). Sourcing
# this file defines the helpers below without running anything, which is what
# that test does.

set -euo pipefail

PATCHES_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/patches"

# ---------------------------------------------------------------------------
# Helpers. Kept as functions with no global state so tests/test-apply-patches.sh
# can source this file and exercise each defence on its own.
# ---------------------------------------------------------------------------

# The path prefix git would prepend to (and filter patch paths against) when
# applying inside $1, with the anti-walk-up ceiling in force. Empty is the only
# acceptable answer: it means git treats $1 itself as the top level, or found
# no repository at all. Prints nothing when $1 is not in a repository.
effective_prefix() {
    local target="$1"
    GIT_CEILING_DIRECTORIES="$(dirname "$target")" \
        git -C "$target" rev-parse --show-prefix 2>/dev/null || true
}

# Every `git apply` in this script goes through here. GIT_CEILING_DIRECTORIES
# stops repository discovery from climbing out of $1 into whatever encloses it,
# which is the whole of infra#2849.
git_apply() {
    local target="$1"; shift
    GIT_CEILING_DIRECTORIES="$(dirname "$target")" \
        git -C "$target" -c core.autocrlf=false apply "$@"
}

# True when git announced that it skipped a file. `git apply` prints this to
# STDERR and still exits 0, which is exactly why it has to be pattern-matched
# rather than left to the exit code.
announced_skip() {
    case "$1" in
        *"Skipped patch "*) return 0 ;;
        *) return 1 ;;
    esac
}

# The files a patch claims it will touch, one per line, -p1 stripped.
#
# Only a "--- " line IMMEDIATELY followed by "+++ " and then "@@ " counts as a
# file header. A removed line whose content happens to start with "-- " would
# otherwise be misread as one. /dev/null (file creation or deletion) is dropped
# from the list; the other side of the pair carries the real path.
patch_target_paths() {
    local prog
    prog=$(cat <<'AWK'
{ line[NR] = $0 }
function clean(s) {
    s = substr(s, index(s, " ") + 1)
    sub(/\t.*$/, "", s)                 # traditional diffs append a timestamp
    return s
}
function emit(p) {
    if (p == "/dev/null") return
    if (substr(p, 1, 1) == "\"") {      # git quotes paths with spaces or
        print "UNPARSEABLE " p          # non-ASCII; refuse rather than guess
        return
    }
    sub(/^[^\/]*\//, "", p)             # -p1
    if (p != "") print p
}
END {
    for (i = 1; i <= NR; i++) {
        if (substr(line[i], 1, 4)   == "--- " &&
            substr(line[i+1], 1, 4) == "+++ " &&
            substr(line[i+2], 1, 3) == "@@ ") {
            emit(clean(line[i]))
            emit(clean(line[i+1]))
        }
    }
}
AWK
)
    awk "$prog" "$1" | sort -u
}

# A fingerprint of one path inside a tree: its content hash, or the literal
# ABSENT. Comparing this before and after covers modification, creation and
# deletion with one rule, and needs nothing from git.
path_state() {
    local target="$1" rel="$2"
    if [ -f "$target/$rel" ]; then
        sha256sum < "$target/$rel" | cut -d' ' -f1
    elif [ -e "$target/$rel" ]; then
        echo "EXISTS-NOT-A-FILE"
    else
        echo "ABSENT"
    fi
}

# The message for defence 3a. Same voice as the DOES NOT APPLY block below.
skip_is_fatal() { # tree_name  patch_name  git_output
    local tree_name="$1" name="$2" out="$3"
    echo "::error::patches/$tree_name/$name was SKIPPED, not applied"
    echo "$out" | sed 's/^/::error::  /'
    echo "::error::A skipped file is a file git decided was none of its business:"
    echo "::error::it resolved the patch's paths against a DIFFERENT repository"
    echo "::error::than $tree_name, so those paths fell outside the directory it"
    echo "::error::was pointed at. It then exits 0. Nothing was modified."
    echo "::error::This is infra#2849. If it is back, the ceiling that stops git"
    echo "::error::walking up out of the target tree has stopped working - see"
    echo "::error::the WHY block at the top of apply-patches.sh. Do NOT relax the"
    echo "::error::matching and do NOT wave this through to get a build out."
}

# ---------------------------------------------------------------------------

main() {
    local ACORE_ROOT=${1:?usage: apply-patches.sh <acore-root> [--check]}
    local MODE=${2:-apply}
    local VERB

    case "$MODE" in
        apply)   VERB="applied"  ;;
        --check) VERB="verified" ;;
        *) echo "::error::unknown mode '$MODE' (expected --check or nothing)"; exit 2 ;;
    esac

    if [ ! -d "$ACORE_ROOT" ]; then
        echo "::error::acore root '$ACORE_ROOT' does not exist"
        exit 1
    fi
    if [ ! -d "$PATCHES_ROOT" ]; then
        echo "::error::patches root '$PATCHES_ROOT' does not exist"
        exit 1
    fi

    local applied=0 targets=0

    # Sorted so application order is the filename order, and so the log is
    # stable between runs. `find -print | sort` rather than a glob because an
    # empty glob and a missing directory have to be told apart below.
    local tree_dir tree_name target prefix
    while IFS= read -r tree_dir; do
        tree_name=$(basename "$tree_dir")
        targets=$((targets + 1))

        # The one piece of knowledge this script holds: the core fork is the
        # root of the build context, and everything else is a module inside it.
        # Derived rather than tabulated, so a new module needs no edit here.
        if [ "$tree_name" = "azerothcore-wotlk" ]; then
            target="$ACORE_ROOT"
        else
            target="$ACORE_ROOT/modules/$tree_name"
        fi

        # A patch directory naming a tree that is not in the build context is a
        # typo, or a module that was removed from UPSTREAM-PINS.env and left its
        # patches behind. Either way the patches would be silently skipped,
        # which is the exact outcome this script refuses to allow.
        if [ ! -d "$target" ]; then
            echo "::error::patches/$tree_name has no matching tree at $target"
            echo "::error::(expected the core fork as 'azerothcore-wotlk', or a module directory name)"
            exit 1
        fi

        # Defence 2. If git still considers $target a SUBDIRECTORY of some
        # repository, every path in every patch below is about to be resolved
        # against that repository's root instead of this tree - which is
        # infra#2849 exactly. Stop here, before anything is half-applied.
        prefix=$(effective_prefix "$target")
        if [ -n "$prefix" ]; then
            echo "::error::git resolves paths inside $target against an enclosing"
            echo "::error::repository (prefix '$prefix'), not against the tree itself."
            echo "::error::Patch paths would be silently skipped rather than applied."
            echo "::error::This is infra#2849; see the WHY block in apply-patches.sh."
            exit 1
        fi

        shopt -s nullglob
        local patch_files=("$tree_dir"/*.patch)
        shopt -u nullglob

        # An empty patch directory means someone deleted the last patch but
        # left the directory, or added a directory and forgot the patch.
        # Neither is a state to build from quietly.
        if [ ${#patch_files[@]} -eq 0 ]; then
            echo "::error::patches/$tree_name contains no *.patch files"
            exit 1
        fi

        local patch name sum out rel i paths before
        for patch in "${patch_files[@]}"; do
            name=$(basename "$patch")
            sum=$(sha256sum "$patch" | cut -c1-12)

            # What this patch says it will touch. Everything below is checked
            # against this list, so a patch we cannot read is a hard failure
            # rather than a patch we quietly cannot verify.
            paths=()
            while IFS= read -r rel; do
                [ -n "$rel" ] && paths+=("$rel")
            done < <(patch_target_paths "$patch")

            if [ ${#paths[@]} -eq 0 ]; then
                echo "::error::patches/$tree_name/$name names no files this script can parse"
                echo "::error::(expected '--- a/<path>' / '+++ b/<path>' / '@@' file headers)"
                echo "::error::Refusing to apply a patch whose effect cannot be verified."
                exit 1
            fi
            for rel in "${paths[@]}"; do
                if [ "${rel#UNPARSEABLE }" != "$rel" ]; then
                    echo "::error::patches/$tree_name/$name has a quoted path this script will not guess at: ${rel#UNPARSEABLE }"
                    exit 1
                fi
            done

            before=()
            for rel in "${paths[@]}"; do
                before+=("$(path_state "$target" "$rel")")
            done

            # --check FIRST even in apply mode: `git apply` is atomic, but
            # running the dry run separately means the error message a human
            # reads is the clean "does not apply" rather than a half-printed
            # --verbose trace. --verbose is passed because a skip is only
            # announced under it, and a skip has to be seen.
            if ! out=$(git_apply "$target" --check --whitespace=error -p1 \
                    --verbose -- "$patch" 2>&1); then
                echo "$out" | sed 's/^/  /'
                echo "::error::patches/$tree_name/$name DOES NOT APPLY to $tree_name"
                echo "::error::This is the designed failure. Upstream almost certainly changed"
                echo "::error::the code this patch touches - very possibly by fixing it. Read the"
                echo "::error::WHY block at the top of the patch file, then either DELETE the"
                echo "::error::patch (upstream fixed it) or REBASE it onto the new pin. Do not"
                echo "::error::relax the matching to make this pass."
                exit 1
            fi
            # Defence 3a, in BOTH modes: `git apply --check` exits 0 on a skip.
            if announced_skip "$out"; then
                skip_is_fatal "$tree_name" "$name" "$out"
                exit 1
            fi

            if [ "$MODE" != "--check" ]; then
                if ! out=$(git_apply "$target" --whitespace=error -p1 \
                        --verbose -- "$patch" 2>&1); then
                    echo "$out" | sed 's/^/  /'
                    echo "::error::patches/$tree_name/$name passed --check and then failed to apply"
                    exit 1
                fi
                if announced_skip "$out"; then
                    skip_is_fatal "$tree_name" "$name" "$out"
                    exit 1
                fi

                # Defence 3b. The check that does not trust git, this script,
                # or the format of the patch: the bytes on disk moved, or this
                # patch did not happen. Every file the patch names must have
                # changed - a patch has at least one hunk per file it lists.
                for i in "${!paths[@]}"; do
                    rel="${paths[$i]}"
                    if [ "$(path_state "$target" "$rel")" = "${before[$i]}" ]; then
                        echo "::error::patches/$tree_name/$name reported success but $rel is BYTE-FOR-BYTE UNCHANGED"
                        echo "::error::git said it applied this patch and the file did not move."
                        echo "::error::Whatever the mechanism, the fix this patch carries is NOT"
                        echo "::error::in this tree and must not be compiled as if it were."
                        echo "::error::This check exists because of infra#2849, where three"
                        echo "::error::patches were reported applied for days without ever"
                        echo "::error::modifying a single byte of any binary that shipped."
                        exit 1
                    fi
                done
            fi

            applied=$((applied + 1))
            echo "  $VERB  $tree_name/$name  sha256:$sum  (${#paths[@]} file(s))"
        done
    done < <(find "$PATCHES_ROOT" -mindepth 1 -maxdepth 1 -type d | sort)

    echo "$VERB $applied patch(es) across $targets upstream tree(s)"

    # Zero patches is a legitimate state (every fix upstreamed), but it must be
    # stated out loud rather than inferred from a silent step.
    if [ "$applied" -eq 0 ]; then
        echo "no local patches are carried against the pinned upstreams"
    fi
}

# Sourced by the test harness (which then calls the helpers directly);
# executed by the build. Same source-guard as .github/scripts/emit-pulumi-event.sh.
if [ "${BASH_SOURCE[0]:-$0}" = "${0}" ]; then
    main "$@"
fi
