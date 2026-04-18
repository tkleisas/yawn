#!/usr/bin/env bash
# Install YAWN's desktop entry and icon into the current user's ~/.local share.
# After running this, YAWN will appear in your app menu / dock with the proper
# icon instead of a generic one.
#
# Works on GNOME, KDE, and any FDO-compliant desktop. Requires the YAWN binary
# to be on your PATH (or edit the Exec= line to an absolute path).

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APPS_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/512x512/apps"

mkdir -p "$APPS_DIR" "$ICON_DIR"

echo "Installing desktop entry  → $APPS_DIR/com.yawn.daw.desktop"
cp -f "$SCRIPT_DIR/com.yawn.daw.desktop" "$APPS_DIR/com.yawn.daw.desktop"

echo "Installing icon           → $ICON_DIR/com.yawn.daw.png"
cp -f "$REPO_ROOT/assets/icon.png" "$ICON_DIR/com.yawn.daw.png"

# Refresh desktop/icon caches if the tools are available.
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
fi

echo
echo "Done. Log out/in (or restart your shell) if the icon doesn't appear immediately."
