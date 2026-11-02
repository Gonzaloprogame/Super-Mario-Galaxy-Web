#!/usr/bin/env bash
# Sets up the "Terminal en directo" feed for a Claude Code session in this repository (docs/DIRECTO.md):
# a checkout of the `directo` branch and the hooks in .claude/settings.local.json (not committed) that run
# tools/directo/terminal_feed.py after each command. Usage: tools/directo/setup.sh [feed-dir]
#   DIRECTO_TRAILER  optional trailer for the feed commits (literal \n between lines)
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
feed="${1:-$HOME/.directo/feed}"
mkdir -p "$(dirname "$feed")"

if [ ! -d "$feed/.git" ] && [ ! -f "$feed/.git" ]; then
  if git -C "$repo" fetch -q origin directo 2>/dev/null; then
    git -C "$repo" worktree add -q -B directo "$feed" origin/directo
  else
    git -C "$repo" worktree add -q --orphan -b directo "$feed"
  fi
fi

mkdir -p "$repo/.claude"
FEED="$feed" REPO="$repo" python3 - <<'EOF'
import json, os, shlex
settings = os.path.join(os.environ['REPO'], '.claude', 'settings.local.json')
try:
    config = json.load(open(settings))
except (OSError, ValueError):
    config = {}
env = f"DIRECTO_DIR={shlex.quote(os.environ['FEED'])} "
if os.environ.get('DIRECTO_TRAILER'):
    env += f"DIRECTO_TRAILER={shlex.quote(os.environ['DIRECTO_TRAILER'])} "
script = shlex.quote(os.path.join(os.environ['REPO'], 'tools', 'directo', 'terminal_feed.py'))
hook = lambda event: [{"type": "command", "command": f"{env}python3 {script} {event} 2>/dev/null || true", "timeout": 10}]
hooks = config.setdefault('hooks', {})
hooks['PostToolUse'] = [h for h in hooks.get('PostToolUse', []) if 'terminal_feed.py' not in json.dumps(h)] + \
    [{"matcher": "Bash|Edit|Write", "hooks": hook('post')}]
hooks['PostToolUseFailure'] = [h for h in hooks.get('PostToolUseFailure', []) if 'terminal_feed.py' not in json.dumps(h)] + \
    [{"matcher": "Bash|Edit|Write", "hooks": hook('fail')}]
hooks['Stop'] = [h for h in hooks.get('Stop', []) if 'terminal_feed.py' not in json.dumps(h)] + [{"hooks": hook('stop')}]
json.dump(config, open(settings, 'w'), indent=2)
EOF
echo "Feed: $feed (rama directo). Hooks en .claude/settings.local.json; abre /hooks o reinicia si no se activan."
