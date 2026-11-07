"""utmt_cli.py - locate UndertaleModCli for the modloader's Python tools on Windows and Linux.

Order: $UTMT_CLI, then (Windows) <game>/mods/tools/utmt/UndertaleModCli.exe, (Linux/macOS) a
native `utmtcli` / `UndertaleModCli` on PATH, else the Windows .exe through wine ($WINE).
With wine, pass every path argument / path env value through `cli.path()` (Wine maps / to Z:).
"""
import os
import shutil
import sys


class UtmtCli:
    def __init__(self, prefix, wine=False):
        self.prefix, self.wine = list(prefix), wine

    def path(self, p):
        if not self.wine:
            return p
        return "Z:" + os.path.abspath(p).replace("/", "\\")

    def cmd(self, *args):
        return self.prefix + list(args)

    def env(self, **paths):
        """os.environ plus path-valued variables, converted for the CLI."""
        env = dict(os.environ, **{k: self.path(v) for k, v in paths.items()})
        if self.wine:
            env.setdefault("WINEDEBUG", "-all")
        return env


def find(game):
    exe = os.path.join(game, "mods", "tools", "utmt", "UndertaleModCli.exe")
    if os.environ.get("UTMT_CLI"):
        custom = os.environ["UTMT_CLI"]
        if os.name != "nt" and custom.lower().endswith(".exe"):
            return UtmtCli(_wine(custom), wine=True)
        return UtmtCli([custom])
    if os.name == "nt":
        if os.path.exists(exe):
            return UtmtCli([exe])
        sys.exit(f"missing {exe}")
    for name in ("utmtcli", "UndertaleModCli"):
        native = shutil.which(name)
        if native:
            return UtmtCli([native])
    if os.path.exists(exe) and shutil.which(os.environ.get("WINE", "wine")):
        return UtmtCli(_wine(exe), wine=True)
    sys.exit(f"no UndertaleModCli: set UTMT_CLI, put the Linux CLI on PATH (utmtcli), or install wine for {exe}")


def _wine(exe):
    return [os.environ.get("WINE", "wine"), exe]
