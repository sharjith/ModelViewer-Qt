#!/usr/bin/env bash
# Publishes locally-built release artifacts to GitHub Releases via `gh`,
# replacing the old CI-driven release job (see .github/workflows/build.yml's
# header comment - CI is now workflow_dispatch-only, not part of the release
# path). Works from Windows (git-bash), WSL, or native Linux - it's just git
# and `gh` calls, no build/compile steps.
#
# Usage:
#   publish-release.sh <tag> --tag                 One-time: create + push the git tag
#   publish-release.sh <tag> <artifact-file>        Create draft (if needed) + upload artifact
#   publish-release.sh <tag> --finalize             Undraft the release once all artifacts are up
#
# Env:
#   RELEASE_NOTES   Path to notes file for the initial `gh release create` (default: CHANGELOG.md)
#
# Examples:
#   ./packaging/publish-release.sh Release-2026.7.1 --tag
#   ./packaging/publish-release.sh Release-2026.7.1 dist/ModelViewer-2026.7.1-Win-X64-installer.exe
#   ./packaging/publish-release.sh Release-2026.7.1 out/build/linux_system_release/modelviewer_2026.7.1_amd64.deb
#   ./packaging/publish-release.sh Release-2026.7.1 --finalize

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
  sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

if [[ $# -lt 2 ]]; then
  usage
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "error: gh CLI not found on PATH (see: winget install GitHub.cli / apt install gh)" >&2
  exit 1
fi

TAG="$1"
ACTION="$2"
NOTES_FILE="${RELEASE_NOTES:-$REPO_ROOT/CHANGELOG.md}"

case "$ACTION" in
  --tag)
    git -C "$REPO_ROOT" tag "$TAG"
    git -C "$REPO_ROOT" push origin "$TAG"
    echo "Tagged and pushed $TAG"
    ;;
  --finalize)
    gh release edit "$TAG" --draft=false
    echo "Release $TAG is now published (no longer a draft)"
    ;;
  *)
    ARTIFACT="$ACTION"
    if [[ ! -f "$ARTIFACT" ]]; then
      echo "error: artifact not found: $ARTIFACT" >&2
      exit 1
    fi

    if gh release view "$TAG" >/dev/null 2>&1; then
      echo "Release $TAG already exists, uploading to it"
    else
      if [[ ! -f "$NOTES_FILE" ]]; then
        echo "error: notes file not found: $NOTES_FILE (set RELEASE_NOTES=... or run --tag first)" >&2
        exit 1
      fi
      TITLE="v${TAG#Release-}"
      echo "Creating draft release $TAG ($TITLE)"
      gh release create "$TAG" --draft --title "$TITLE" --notes-file "$NOTES_FILE"
    fi

    echo "Uploading $ARTIFACT"
    gh release upload "$TAG" "$ARTIFACT" --clobber
    echo "Uploaded. Once every platform's artifact is up, run:"
    echo "  $0 $TAG --finalize"
    ;;
esac
