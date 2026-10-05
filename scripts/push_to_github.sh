#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

echo "=========================================================="
echo "    Pushing CowFS to GitHub (Abhijit-Sahoo-maker)         "
echo "=========================================================="

# Check if remote exists
REMOTE_URL=$(git remote get-url origin 2>/dev/null || echo "")

if [ -z "$REMOTE_URL" ]; then
    git remote add origin https://github.com/Abhijit-Sahoo-maker/Copy-on-Write-Virtual-Filesystem-Engine.git
fi

echo "Current remote: $(git remote get-url origin)"
echo "Pushing branch 'main' to GitHub..."
echo "(If prompted for password, paste your GitHub Personal Access Token)"

git push -u origin main

echo "=========================================================="
echo "[+] Successfully pushed to GitHub!"
echo "View your repository at:"
echo "https://github.com/Abhijit-Sahoo-maker/Copy-on-Write-Virtual-Filesystem-Engine"
echo "=========================================================="
