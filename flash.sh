#!/usr/bin/env bash
# flash.sh — Build + flash dispatcher for monorepo apps
#
# Usage:
#   ./flash.sh [app] [options]
#   ./flash.sh radio -m        Build, flash, and monitor the radio app
#   ./flash.sh homekit          Build and flash the homekit app
#
# If no app is specified, defaults to "radio".
# All other arguments are passed through to the app's flash.sh.

set -e

cd "$(dirname "$0")"

APP="radio"

# Check if first arg is an app name (not a flag)
if [ $# -gt 0 ] && [[ "$1" != -* ]]; then
  APP="$1"
  shift
fi

APP_DIR="apps/$APP"
if [ ! -d "$APP_DIR" ]; then
  echo "Error: App '$APP' not found at $APP_DIR"
  echo "Available apps:"
  ls -1 apps/ 2>/dev/null || echo "  (none)"
  exit 1
fi

if [ -f "$APP_DIR/flash.sh" ]; then
  exec "$APP_DIR/flash.sh" "$@"
else
  echo "Error: No flash.sh found for app '$APP'"
  exit 1
fi
