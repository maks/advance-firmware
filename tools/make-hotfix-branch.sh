#!/usr/bin/env bash
#
# make-hotfix-branch.sh
#
# Creates a hotfix/cherrypick branch from a base branch and cherry-picks all
# merged PRs that carry the "cherrypick" label AND the given milestone.
#
# Usage:
#   ./make-hotfix-branch.sh <milestone> <base-branch>
#
# Example:
#   ./make-hotfix-branch.sh 3.1.2 master
#
# The new branch is named "<milestone>-cherrypicks" and is created from
# <base-branch> (a local branch name; the local branch is required because
# the script checks it out).
#
# For every matching PR the corresponding commit on master is located
# (squash-merge style "(#NNN)" reference or "Merge pull request #NNN"
# message) and cherry-picked onto the new branch in the order the commits
# sit in master's history (oldest first). Genuine merge commits are picked
# with -m 1.
#
# The master ref is refreshed from the remote (origin, or the first
# configured remote if there is no origin) before it is used. Override it
# with the MASTER_REF environment variable (e.g. MASTER_REF=advance/master);
# when overridden, no fetch is attempted.
#
# Requirements: bash >= 4, git, an authenticated `gh` CLI, jq.

set -euo pipefail

LABEL="cherrypick"

err() { printf 'ERROR: %s\n' "$*" >&2; }
die() { err "$*"; exit 1; }

usage() {
  cat <<'EOF'
Usage: make-hotfix-branch.sh <milestone> <base-branch>

Creates branch "<milestone>-cherrypicks" from <base-branch> and
cherry-picks all merged PRs labelled "cherrypick" that belong to the
given milestone, in merge order.

Arguments:
  <milestone>      GitHub milestone title (e.g. 3.1.2)
  <base-branch>    Local branch the new branch is created from (e.g. master)

Environment:
  MASTER_REF       Ref used to look up PR commits on master
                   (default: fetched from origin, or the first remote)
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
  usage
  exit 0
fi
if [[ $# -ne 2 ]]; then
  usage >&2
  exit 2
fi

MILESTONE="$1"
BASE_BRANCH="$2"
NEW_BRANCH="${MILESTONE}-cherrypicks"

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
for dep in git gh jq; do
  command -v "$dep" >/dev/null 2>&1 || die "required dependency '$dep' not found in PATH"
done

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not inside a git working tree"

# The base must be a local branch: it is checked out, and it is required to
# be clean (a dirty checkout cannot be switched away from).
if ! git show-ref --verify --quiet "refs/heads/${BASE_BRANCH}"; then
  if git show-ref --verify --quiet "refs/remotes/${BASE_BRANCH}"; then
    die "base branch '${BASE_BRANCH}' is a remote-tracking ref; check out a local branch (e.g. git checkout '${BASE_BRANCH}')"
  fi
  die "base branch '${BASE_BRANCH}' not found (check out or fetch it first)"
fi
if [[ "$(git branch --show-current)" == "${NEW_BRANCH}" ]]; then
  die "cannot create '${NEW_BRANCH}' while it is already checked out"
fi

if [[ -n "$(git status --porcelain)" ]]; then
  die "working tree is dirty - commit or stash changes before running"
fi

if git show-ref --verify --quiet "refs/heads/${NEW_BRANCH}"; then
  die "branch '${NEW_BRANCH}' already exists - delete it or pick another milestone"
fi

# A commit is a genuine merge commit if it has two or more parents.
is_merge_commit() {
  local n
  n="$(git cat-file -p "$1" 2>/dev/null | grep -c '^parent ' || true)"
  [[ "$n" -ge 2 ]]
}

# ---------------------------------------------------------------------------
# Fetch matching PRs (label + milestone), merged into the default branch
# ---------------------------------------------------------------------------
PR_JSON="$(gh pr list \
  --label "$LABEL" \
  --state merged \
  --search "milestone:\"${MILESTONE}\"" \
  --limit 500 \
  --json number,title,mergedAt,mergeCommit)"

[[ -n "$PR_JSON" && "$PR_JSON" != "[]" ]] \
  || die "no merged PRs found with label '${LABEL}' and milestone '${MILESTONE}'"

# PR number, title, the merge commit SHA and the merge time (ISO text plus
# epoch seconds from jq, so no platform `date` parsing is needed), one PR per
# line (tab separated).
PR_FIELDS="$(printf '%s\n' "$PR_JSON" | jq -r '
  .[]
  | [.number,
     (.title // ""),
     (.mergeCommit.oid // ""),
     (.mergedAt // ""),
     ((.mergedAt // "") | try fromdate catch 0)]
  | @tsv')"

# ---------------------------------------------------------------------------
# Resolve and refresh the master ref (overridable via the MASTER_REF env var)
# ---------------------------------------------------------------------------
# MASTER_REF (display name) and MASTER_SHA (resolved commit) are set below.
if [[ -n "${MASTER_REF:-}" ]]; then
  # Explicit override: accept branch names (master), remote-tracking names
  # (advance/master) or full refs (refs/remotes/advance/master).
  if ! git rev-parse --verify --quiet "${MASTER_REF}" >/dev/null 2>&1; then
    die "MASTER_REF='${MASTER_REF}' does not resolve to a commit"
  fi
  MASTER_SHA="$(git rev-parse --verify "${MASTER_REF}")"
else
  # Prefer the remote master, refreshed from the network, so we never search
  # a stale local copy.
  REMOTE_NAME="origin"
  if ! git remote | grep -qx "origin"; then
    REMOTE_NAME="$(git remote | head -n 1 || true)"
  fi
  if [[ -n "$REMOTE_NAME" ]] && git fetch --quiet "$REMOTE_NAME" master 2>/dev/null; then
    MASTER_REF="${REMOTE_NAME}/master"
    echo "Fetched ${MASTER_REF} (remote master is up to date)."
  elif [[ -n "$REMOTE_NAME" ]] && git rev-parse --verify --quiet "refs/remotes/${REMOTE_NAME}/master" >/dev/null 2>&1; then
    MASTER_REF="${REMOTE_NAME}/master"
    echo "WARN: could not fetch ${REMOTE_NAME}/master; using the local remote-tracking ref" >&2
  elif git rev-parse --verify --quiet refs/heads/master >/dev/null 2>&1; then
    MASTER_REF="master"
    echo "WARN: no remote master available; using local branch 'master'" >&2
  else
    die "cannot resolve a master ref (no fetchable remote master, no origin/master, no local master)"
  fi
  MASTER_SHA="$(git rev-parse --verify "${MASTER_REF}")"
fi

# ---------------------------------------------------------------------------
# Master history lookups
# ---------------------------------------------------------------------------
# True if $1 is an ancestor of $2 (both must be commits/refs).
is_ancestor() {
  git merge-base --is-ancestor "$1" "$2" >/dev/null 2>&1
}

# Locate the commit on master that closed/merged PR #$1.
# Squash merges:  subject contains "(#NNN)"
# Merge commits:  subject contains "Merge pull request #NNN from ..."
find_pr_commit() {
  local pr_num="$1" sha
  sha="$(git log -E -n 1 --format=%H \
      --grep "\(#${pr_num}\)|pull request #${pr_num} from" \
      "${MASTER_SHA}" 2>/dev/null || true)"
  printf '%s' "$sha"
}

# Resolve the commit in master's history that carries a PR.
#   $1: PR number, $2: merge commit SHA reported by the GitHub API
# Echoes a full SHA, or nothing when the commit cannot be identified. The API
# SHA is only trusted when it really is part of master's history (the API
# reports the most recent merge, which may post-date or miss this ref).
resolve_pr_commit() {
  local pr_num="$1" api_sha="$2" sha
  sha="$(find_pr_commit "$pr_num")"
  if [[ -n "$sha" ]]; then
    printf '%s' "$sha"
    return 0
  fi
  if [[ -n "$api_sha" ]] \
      && sha="$(git rev-parse --verify --quiet "${api_sha}^{commit}" 2>/dev/null)" \
      && is_ancestor "$sha" "$MASTER_SHA"; then
    echo "    note: no commit on ${MASTER_REF} references #${pr_num}; using GitHub merge SHA ${sha:0:12}" >&2
    printf '%s' "$sha"
    return 0
  fi
  return 0
}

# Master history as "position<TAB>sha<TAB>committer epoch", oldest first
# (position 1 is the oldest commit reachable from MASTER_SHA).
MASTER_COMMITS="$(git rev-list --reverse --format='%ct' "${MASTER_SHA}" \
    | awk '/^commit /{ sha = $2; next } { print ++p "\t" sha "\t" $1 }')"

# Position of commit $1 in master's history (empty if it is not in there).
master_position() {
  [[ -n "$1" ]] || return 0
  awk -v s="$1" 'BEGIN{ FS = "\t" } $2 == s { print $1; exit }' <<< "${MASTER_COMMITS}"
}

# Position of the newest master commit committed at or before epoch $1 (0 if
# none, i.e. before the start of this history).
master_position_at_time() {
  awk -v ts="${1:-0}" 'BEGIN{ FS = "\t" } $3 <= ts { p = $1 } END { print p + 0 }' \
      <<< "${MASTER_COMMITS}"
}

# ---------------------------------------------------------------------------
# Resolve every PR to its commit on ${MASTER_SHA} and order them oldest-first
# ---------------------------------------------------------------------------
# The order comes from the position of the resolved commit in master's history
# (the mergedAt timestamp can disagree when a PR lands on another branch first
# and reaches master later). The commit is resolved here, before sorting, so a
# PR whose API SHA is missing from this ref still sorts by the master commit
# that actually carries it rather than drifting to the end of the list.
#
# The sort key is position * 1000, which leaves room to slot the rare PR that
# cannot be placed in master's history at all between its neighbours by GitHub
# merge time (+500) instead of after everything.
SORTED_PRS="$(while IFS=$'\t' read -r pr_num pr_title merge_commit merged_at merged_epoch; do
    [[ -n "$pr_num" ]] || continue
    commit="$(resolve_pr_commit "$pr_num" "$merge_commit")"
    pos=""
    if [[ -n "$commit" ]]; then
      pos="$(master_position "$commit")"
    fi
    if [[ -n "$pos" ]]; then
      key=$((pos * 1000))
    else
      echo "WARN: could not place PR #${pr_num} in ${MASTER_REF} history; ordering by GitHub merge time ${merged_at:-unknown} as a fallback" >&2
      key=$(( $(master_position_at_time "$merged_epoch") * 1000 + 500 ))
    fi
    printf '%s\t%s\t%s\t%s\n' "$key" "$pr_num" "$pr_title" "$commit"
  done <<< "$PR_FIELDS" | sort -n -k1,1 -k2,2n | cut -f2-)"

TOTAL="$(printf '%s\n' "$SORTED_PRS" | wc -l | tr -d ' ')"
echo ""
echo "Found ${TOTAL} PR(s) to cherry-pick from milestone '${MILESTONE}' (master ref: ${MASTER_REF})."
echo ""

# ---------------------------------------------------------------------------
# Create and check out the hotfix branch (only once every lookup succeeded,
# so a run that bails out early does not leave a stray branch behind)
# ---------------------------------------------------------------------------
echo "Creating and checking out '${NEW_BRANCH}' from '${BASE_BRANCH}'..."
if ! git checkout -q -b "${NEW_BRANCH}" "${BASE_BRANCH}"; then
  git branch --show-current | grep -qx "${NEW_BRANCH}" \
    && echo "NOTE: branch '${NEW_BRANCH}' was created but not checked out - delete it or fix the checkout" >&2
  die "failed to create/check out '${NEW_BRANCH}' from '${BASE_BRANCH}'; no cherry-pick was attempted"
fi
# Belt and braces: never cherry-pick unless the new branch really is checked out.
if [[ "$(git branch --show-current)" != "${NEW_BRANCH}" ]]; then
  die "HEAD is on '$(git branch --show-current)', expected '${NEW_BRANCH}'; no cherry-pick was attempted"
fi
echo ""

applied=()
skipped=()
failed=()

# Each entry already carries the commit resolved (or not) for ${MASTER_REF}.
while IFS=$'\t' read -r pr_num pr_title commit; do
  [[ -n "$pr_num" ]] || continue
  echo "--- PR #${pr_num}: ${pr_title}"

  if [[ -z "$commit" ]]; then
    echo "    SKIP: could not find commit for PR #${pr_num} on ${MASTER_REF}"
    skipped+=("#${pr_num} ${pr_title}")
    continue
  fi

  cp_args=(-x)
  if is_merge_commit "$commit"; then
    cp_args+=(-m 1)
  fi

  if cp_output="$(git cherry-pick "${cp_args[@]}" "$commit" 2>&1)"; then
    new_sha="$(git rev-parse HEAD)"
    applied+=("#${pr_num} ${pr_title} [${commit:0:12} -> ${new_sha:0:12}]")
    echo "    OK: cherry-pick ${commit:0:12} -> ${new_sha:0:12}"
  elif git rev-parse -q --verify CHERRY_PICK_HEAD >/dev/null 2>&1 \
      && [[ -z "$(git diff --name-only --diff-filter=U)" ]]; then
    # Empty pick (change already present in the base): git left the pick in
    # progress but there is nothing to resolve - skip it and carry on.
    echo "    SKIP: ${commit:0:12} produced no changes (already in base); skipping."
    git cherry-pick --skip >/dev/null 2>&1 || true
    skipped+=("#${pr_num} ${pr_title} (no changes; already in base)")
  elif git rev-parse -q --verify CHERRY_PICK_HEAD >/dev/null 2>&1; then
    # Real conflict: git is waiting for manual resolution.
    echo "    CONFLICT: cherry-pick of ${commit:0:12} stopped for manual resolution."
    [[ -n "$cp_output" ]] && printf '%s\n' "$cp_output" | sed 's/^/      /'
    failed+=("#${pr_num} ${pr_title} [${commit:0:12}]")
    break
  else
    # Failed without entering a pick-in-progress state (unexpected).
    echo "    ERROR: cherry-pick of ${commit:0:12} failed:"
    [[ -n "$cp_output" ]] && printf '%s\n' "$cp_output" | sed 's/^/      /'
    failed+=("#${pr_num} ${pr_title} [${commit:0:12}]")
    break
  fi
done <<< "$SORTED_PRS"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "=============================================="
echo " Cherrypick summary"
echo "=============================================="
echo " Milestone : ${MILESTONE}"
echo " Base      : ${BASE_BRANCH}"
echo " Branch    : ${NEW_BRANCH}"
echo " PRs found : ${TOTAL}"
echo " Applied   : ${#applied[@]}"
echo " Skipped   : ${#skipped[@]}"
echo " Failed    : ${#failed[@]}"
echo "----------------------------------------------"

if ((${#applied[@]} > 0)); then
  echo "Applied:"
  for entry in "${applied[@]}"; do echo "  [x] ${entry}"; done
fi
if ((${#skipped[@]} > 0)); then
  echo "Skipped / not applied:"
  for entry in "${skipped[@]}"; do echo "  [ ] ${entry}"; done
fi

if ((${#failed[@]} > 0)); then
  echo "Failed:"
  for entry in "${failed[@]}"; do echo "  [!] ${entry}"; done
  echo "----------------------------------------------"
  echo ""
  if git rev-parse -q --verify CHERRY_PICK_HEAD >/dev/null 2>&1; then
    echo "The cherry-pick is PAUSED at the first failed commit."
    echo "Resolve the conflict (if any), then either:"
    echo "  git cherry-pick --continue   to keep going manually"
    echo "  git cherry-pick --skip       to skip the failing commit"
    echo "  git cherry-pick --abort      to abandon the remaining picks"
  else
    echo "The cherry-pick stopped at the first failed commit and no"
    echo "cherry-pick is in progress; the branch is in a clean state."
  fi
  echo "The branch '${NEW_BRANCH}' is left in its current state."
  exit 1
fi

echo "----------------------------------------------"
echo "All ${#applied[@]} commit(s) cherry-picked successfully."
echo "Current HEAD: $(git rev-parse --short HEAD) on branch '${NEW_BRANCH}'."
