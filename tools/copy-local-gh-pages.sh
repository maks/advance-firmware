#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Copies picoTracker WebAssembly deployable assets to a sibling git repository
# configured with a GitHub Actions workflow for hosting on GitHub Pages.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Target directory defaults to sibling directory ../advance-web
TARGET_DIR="${1:-${SRC_ROOT}/../advance-web}"
TARGET_DIR="$(mkdir -p "${TARGET_DIR}" && cd "${TARGET_DIR}" && pwd)"

echo "=== picoTracker Web Pages Exporter ==="
echo "Source: ${SRC_ROOT}"
echo "Target: ${TARGET_DIR}"

# 1. Verify build artifacts
WASM_JS="${SRC_ROOT}/web/public/wasm/picotracker.js"
WASM_BIN="${SRC_ROOT}/web/public/wasm/picotracker.wasm"

if [[ ! -f "${WASM_JS}" || ! -f "${WASM_BIN}" ]]; then
  echo "Error: WASM build artifacts not found at web/public/wasm/"
  echo "Please build the WASM target first:"
  echo "  ./tools/build-wasm.sh Release"
  exit 1
fi

# 2. Initialize target git repository if needed
if [[ ! -d "${TARGET_DIR}/.git" ]]; then
  echo "Initializing git repository in ${TARGET_DIR}..."
  git -C "${TARGET_DIR}" init -b main
fi

# 3. Clean previous deployable files in target (preserving .git)
echo "Syncing deployable assets..."
find "${TARGET_DIR}" -mindepth 1 -maxdepth 1 ! -name '.git' -exec rm -rf {} +

# 4. Copy static web assets
cp "${SRC_ROOT}/web/index.html" "${TARGET_DIR}/index.html"
cp "${SRC_ROOT}/web/manifest.webmanifest" "${TARGET_DIR}/manifest.webmanifest"
cp "${SRC_ROOT}/web/main.js" "${TARGET_DIR}/main.js"
cp "${SRC_ROOT}/web/input.js" "${TARGET_DIR}/input.js"
cp "${SRC_ROOT}/web/storage.js" "${TARGET_DIR}/storage.js"
cp "${SRC_ROOT}/web/zip.js" "${TARGET_DIR}/zip.js"
cp "${SRC_ROOT}/web/factory-content-credits.txt" "${TARGET_DIR}/factory-content-credits.txt"
cp "${SRC_ROOT}/web/coi-serviceworker.js" "${TARGET_DIR}/coi-serviceworker.js"
cp "${SRC_ROOT}/web/_headers" "${TARGET_DIR}/_headers"
cp -R "${SRC_ROOT}/web/assets" "${TARGET_DIR}/assets"

# 5. Copy WASM binary and JS loader into wasm/ subdirectory
mkdir -p "${TARGET_DIR}/wasm"
cp "${WASM_JS}" "${TARGET_DIR}/wasm/picotracker.js"
cp "${WASM_BIN}" "${TARGET_DIR}/wasm/picotracker.wasm"

# 6. Create .nojekyll to disable Jekyll processing on GitHub Pages
touch "${TARGET_DIR}/.nojekyll"

# 7. Create .gitignore
cat > "${TARGET_DIR}/.gitignore" << 'EOF'
.DS_Store
Thumbs.db
*~
*.swp
EOF

# 8. Create GitHub Actions Pages workflow
mkdir -p "${TARGET_DIR}/.github/workflows"
cat > "${TARGET_DIR}/.github/workflows/deploy.yml" << 'EOF'
name: Deploy picoTracker to GitHub Pages

on:
  push:
    branches: [ main ]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: "pages"
  cancel-in-progress: false

jobs:
  deploy:
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Setup Pages
        uses: actions/configure-pages@v5

      - name: Upload artifact
        uses: actions/upload-pages-artifact@v3
        with:
          path: '.'

      - name: Deploy to GitHub Pages
        id: deployment
        uses: actions/deploy-pages@v4
EOF

# 9. Create README.md
cat > "${TARGET_DIR}/README.md" << 'EOF'
# picoTracker Advance Web

WebAssembly distribution of **picoTracker Advance**.

## Free Hosting via GitHub Pages

This repository is ready to be hosted on GitHub Pages:

1. Create a repository on GitHub (e.g. `advance-web` or `picotracker`).
2. Add your GitHub remote and push:
   ```bash
   git remote add origin git@github.com:<YOUR-USER>/<YOUR-REPO>.git
   git push -u origin main
   ```
3. In your GitHub repository:
   - Go to **Settings** &rarr; **Pages**.
   - Under **Build and deployment** &rarr; **Source**, select **GitHub Actions**.
4. The workflow in `.github/workflows/deploy.yml` will automatically deploy your site!

### Cross-Origin Isolation (SharedArrayBuffer)

picoTracker utilizes WebAssembly pthreads and AudioWorklet which require Cross-Origin Isolation (`COOP`/`COEP` headers).
- **GitHub Pages**: Enabled automatically via the included `coi-serviceworker.js`.
- **Cloudflare Pages / Netlify**: Supported natively via the included `_headers` configuration.
EOF

# 10. Stage files in git
git -C "${TARGET_DIR}" add -A

echo ""
echo "=== Successfully prepared ${TARGET_DIR} ==="
echo ""
echo "Files copied:"
ls -lh "${TARGET_DIR}"
echo ""
echo "Next steps to publish on GitHub Pages:"
echo "  cd ${TARGET_DIR}"
echo "  git commit -m 'Initial picoTracker web release'"
echo "  git remote add origin git@github.com:<username>/<repo>.git"
echo "  git push -u origin main"
echo ""
echo "Then enable GitHub Actions as the Pages source in GitHub repo settings:"
echo "  Settings -> Pages -> Source -> GitHub Actions"
