#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

usage() {
    cat <<'USAGE'
Usage:
  scripts/release_macos.sh [--version VERSION] [--allow-dirty] [--no-push]

Builds GridCollider Release for macOS, creates dist/GridCollider-VERSION-macOS.zip,
pushes main, creates/pushes tag vVERSION, and creates or updates the GitHub Release
asset for that tag.

Options:
  --version VERSION   Override the version read from CMakeLists.txt.
  --allow-dirty      Allow releasing with uncommitted tracked changes.
  --no-push          Build and zip only; do not push, tag, or upload.
USAGE
}

VERSION=""
ALLOW_DIRTY=0
NO_PUSH=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            VERSION="${2:-}"
            shift 2
            ;;
        --allow-dirty)
            ALLOW_DIRTY=1
            shift
            ;;
        --no-push)
            NO_PUSH=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    VERSION="$(awk '
        $1 == "VERSION" {
            gsub(/[^0-9.]/, "", $2);
            print $2;
            exit;
        }
    ' CMakeLists.txt)"
fi

if [[ -z "$VERSION" ]]; then
    echo "Could not determine version from CMakeLists.txt" >&2
    exit 1
fi

TAG="v${VERSION}"
ZIP_PATH="dist/GridCollider-${VERSION}-macOS.zip"
APP_PATH="build-release/GridCollider_artefacts/Release/GridCollider.app"

if [[ "$NO_PUSH" -eq 0 ]]; then
    command -v gh >/dev/null 2>&1 || { echo "GitHub CLI (gh) is required for release upload" >&2; exit 1; }
    gh auth status >/dev/null
fi

if [[ "$ALLOW_DIRTY" -eq 0 && "$NO_PUSH" -eq 0 ]]; then
    if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
        echo "Tracked working tree changes are present. Commit them first or use --allow-dirty." >&2
        git status --short --untracked-files=no >&2
        exit 1
    fi
fi

echo "Configuring Release build..."
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release

echo "Building GridCollider ${VERSION}..."
cmake --build build-release --config Release

if [[ ! -d "$APP_PATH" ]]; then
    echo "Release app was not produced at $APP_PATH" >&2
    exit 1
fi

mkdir -p dist
rm -f "$ZIP_PATH"

echo "Creating ${ZIP_PATH}..."
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$ZIP_PATH"
ls -lh "$ZIP_PATH"

if [[ "$NO_PUSH" -eq 1 ]]; then
    echo "Built local release zip only (--no-push)."
    exit 0
fi

REMOTE_URL="$(git remote get-url origin)"
REPO_SLUG="$(echo "$REMOTE_URL" | sed -E 's#^git@github.com:##; s#^https://github.com/##; s#\.git$##')"

if [[ -z "$REPO_SLUG" || "$REPO_SLUG" == "$REMOTE_URL" ]]; then
    echo "Could not infer GitHub repo from origin: $REMOTE_URL" >&2
    exit 1
fi

echo "Pushing current branch..."
git push origin HEAD

if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "Tag ${TAG} already exists locally."
else
    git tag -a "$TAG" -m "GridCollider ${VERSION} macOS release build"
fi

echo "Pushing tag ${TAG}..."
git push origin "$TAG"

if gh release view "$TAG" --repo "$REPO_SLUG" >/dev/null 2>&1; then
    echo "Updating GitHub Release asset..."
    gh release upload "$TAG" "$ZIP_PATH" --repo "$REPO_SLUG" --clobber
else
    echo "Creating GitHub Release..."
    gh release create "$TAG" "$ZIP_PATH" \
        --repo "$REPO_SLUG" \
        --title "GridCollider ${VERSION}" \
        --notes "GridCollider ${VERSION} macOS release build."
fi

echo "Release ready: https://github.com/${REPO_SLUG}/releases/tag/${TAG}"
