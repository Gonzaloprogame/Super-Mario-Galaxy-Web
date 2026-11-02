#!/usr/bin/env python3
"""Feed for the "Terminal en directo" box of web/shell/overlay.html.

Claude Code hooks call this script after every Bash/Edit/Write tool call and when a turn ends. It appends one line
(the command or the edited file, never its output) to terminal.json in a checkout of the `directo` branch, and a
detached pusher publishes that branch at most every PUSH_EVERY seconds. The overlay reads the file from
raw.githubusercontent.com.

Only the command line and its description are published, after redaction: no output (it can contain game data),
no secrets, and local paths shortened. Setup and the hook configuration are in docs/DIRECTO.md.

    terminal_feed.py post|fail|stop   (hook payload on stdin)
    terminal_feed.py push             (internal: the detached pusher)
Environment: DIRECTO_DIR = checkout of the `directo` branch; DIRECTO_TRAILER = optional commit trailer
(literal \\n between lines).
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone

KEEP = 40          # lines kept in terminal.json
PUSH_EVERY = 15    # seconds between pushes
MAX_CMD = 180      # characters of a command shown

REDACT = [
    (re.compile(r'\b(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})'), '[secreto]'),
    (re.compile(r'(?i)\b(authorization|bearer|token|password|passwd|secret|api[_-]?key)\b(\s*[:=]\s*|\s+)\S+'),
     r'\1\2[secreto]'),
    (re.compile(r'(?i)(https?://)[^/\s:@]+:[^/\s@]+@'), r'\1[secreto]@'),
    (re.compile(r'[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'), '[correo]'),
]


def shorten_paths(text):
    text = re.sub(r'/tmp/claude-0/[^\s/]+/[^\s/]+/scratchpad', '$S', text)
    text = re.sub(r'/tmp/claude-0/[^\s"\']+', '$TMP', text)
    text = text.replace('/home/user/Super-Mario-Galaxy-Web', '~/repo')
    return text.replace('/home/user', '~').replace('/root', '~')


def clean(text, limit):
    lines = (text or '').strip().splitlines() or ['']
    first = lines[0]
    for pattern, repl in REDACT:
        first = pattern.sub(repl, first)
    first = shorten_paths(first)
    if len(first) > limit:
        first = first[:limit - 1] + '…'
    if len(lines) > 1:
        first += f'  … (+{len(lines) - 1} líneas)'
    return first


def entry(event, payload):
    tool = payload.get('tool_name', '')
    data = payload.get('tool_input') or {}
    now = datetime.now(timezone.utc).isoformat(timespec='seconds')
    if tool == 'Bash':
        response = payload.get('tool_response')
        failed = event == 'fail' or (isinstance(response, dict) and response.get('interrupted'))
        return {'t': now, 'tipo': 'bash', 'desc': clean(data.get('description', ''), 120),
                'cmd': clean(data.get('command', ''), MAX_CMD), 'ok': not failed}
    if tool in ('Edit', 'Write'):
        return {'t': now, 'tipo': 'editar' if tool == 'Edit' else 'escribir', 'desc': '',
                'cmd': clean(data.get('file_path', ''), MAX_CMD), 'ok': event != 'fail'}
    return None


def record(event, payload, root):
    feed_path = os.path.join(root, 'terminal.json')
    with open(os.path.join(root, '..', 'directo.lock'), 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            with open(feed_path) as f:
                feed = json.load(f)
        except (OSError, ValueError):
            feed = {'lineas': []}
        if event == 'stop':
            feed['estado'] = 'en espera'
        else:
            line = entry(event, payload)
            if line is None:
                return
            feed['lineas'] = (feed.get('lineas', []) + [line])[-KEEP:]
            feed['estado'] = 'trabajando'
        feed['actualizado'] = datetime.now(timezone.utc).isoformat(timespec='seconds')
        tmp = feed_path + '.tmp'
        with open(tmp, 'w') as f:
            json.dump(feed, f, ensure_ascii=False, indent=1)
        os.replace(tmp, feed_path)
    # One pending pusher at a time; it publishes whatever the file holds when it runs
    marker = os.path.join(root, '..', 'directo.pending')
    try:
        fd = os.open(marker, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        os.close(fd)
    except FileExistsError:
        return
    subprocess.Popen([sys.executable, os.path.abspath(__file__), 'push'], env=dict(os.environ, DIRECTO_DIR=root),
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)


def push(root):
    stamp = os.path.join(root, '..', 'directo.lastpush')
    try:
        wait = os.path.getmtime(stamp) + PUSH_EVERY - time.time()
    except OSError:
        wait = 0
    if wait > 0:
        time.sleep(wait)
    os.unlink(os.path.join(root, '..', 'directo.pending'))  # later writes start a new pusher
    log = open(os.path.join(root, '..', 'directo.log'), 'a')
    with open(os.path.join(root, '..', 'directo.git.lock'), 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        git = lambda *args: subprocess.run(['git', '-C', root, *args], stdout=log, stderr=log)
        git('add', 'terminal.json')
        if git('diff', '--cached', '--quiet').returncode != 0:
            trailer = os.environ.get('DIRECTO_TRAILER', '').replace('\\n', '\n')
            git('commit', '-q', '-m', 'terminal: actualizar' + (f'\n\n{trailer}' if trailer else ''))
            for delay in (0, 2, 4, 8):
                time.sleep(delay)
                if git('push', '-q', 'origin', 'directo').returncode == 0:
                    break
        open(stamp, 'w').close()


def main():
    root = os.environ.get('DIRECTO_DIR')
    if not root or not os.path.isdir(root):
        return  # not set up: do nothing, never break the session
    event = sys.argv[1] if len(sys.argv) > 1 else 'post'
    if event == 'push':
        push(root)
        return
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        payload = {}
    record(event, payload, root)


if __name__ == '__main__':
    main()
