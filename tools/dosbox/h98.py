#!/usr/bin/env python3
"""Harness to observe original games in DOSBox-X (PC-98 mode) under Xvfb.

Black-box observation only: launches the game, sends keys, captures screenshots.

Usage as a library:
    with Session(gamedir, ["MUSIC", "RPG"]) as s:
        s.wait(3); s.shot("title.png"); s.key("Return"); ...
CLI:
    h98.py GAMEDIR OUTDIR "cmd1;cmd2" "script"
    script words: N(seconds float) | key:NAME | hold:NAME:secs | shot:NAME
"""
import os
import subprocess
import sys
import time

CONF = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pc98.conf')


class Session:
    def __init__(self, gamedir, commands, display=99, extra_conf=None):
        self.display = ':%d' % display
        self.gamedir = gamedir
        self.commands = commands
        self.extra = extra_conf or []

    def __enter__(self):
        self.xvfb = subprocess.Popen(['Xvfb', self.display, '-screen', '0', '1024x768x24'],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)
        env = dict(os.environ, DISPLAY=self.display, SDL_AUDIODRIVER='dummy')
        args = ['dosbox-x', '-conf', CONF, '-nomenu', '-fastlaunch']
        for e in self.extra:
            args += ['-set', e]
        args += ['-c', 'mount c "%s"' % self.gamedir, '-c', 'c:']
        for c in self.commands:
            args += ['-c', c]
        self.log = open('/tmp/claude-0/dosbox_%s.log' % self.display[1:], 'w')
        self.proc = subprocess.Popen(args, env=env, stdout=self.log, stderr=subprocess.STDOUT)
        time.sleep(2.0)
        self.win = None
        for _ in range(20):
            r = subprocess.run(['xdotool', 'search', '--class', 'dosbox'], env=env, capture_output=True, text=True)
            ids = r.stdout.split()
            if ids:
                self.win = ids[-1]
                break
            time.sleep(0.5)
        self.env = env
        return self

    def __exit__(self, *a):
        for p in (self.proc, self.xvfb):
            try:
                p.terminate()
                p.wait(timeout=5)
            except Exception:
                p.kill()
        self.log.close()

    def wait(self, sec):
        time.sleep(sec)

    def key(self, name, hold=0.08):
        subprocess.run(['xdotool', 'keydown', '--window', self.win, name], env=self.env)
        time.sleep(hold)
        subprocess.run(['xdotool', 'keyup', '--window', self.win, name], env=self.env)

    def type(self, text):
        subprocess.run(['xdotool', 'type', '--window', self.win, '--delay', '60', text], env=self.env)

    def shot(self, path):
        subprocess.run(['import', '-display', self.display, '-window', self.win or 'root', path], env=self.env,
                       capture_output=True)


def main():
    gamedir, outdir, cmds, script = sys.argv[1:5]
    os.makedirs(outdir, exist_ok=True)
    disp = int(os.environ.get('H98_DISPLAY', '99'))
    with Session(gamedir, [c for c in cmds.split(';') if c], display=disp) as s:
        for w in script.split():
            if w.startswith('key:'):
                s.key(w[4:])
            elif w.startswith('hold:'):
                _, k, t = w.split(':')
                s.key(k, float(t))
            elif w.startswith('shot:'):
                s.shot(os.path.join(outdir, w[5:] + '.png'))
            else:
                s.wait(float(w))


if __name__ == '__main__':
    main()
