#!/usr/bin/env python3
"""Loose sound overrides through LooseModCompiler (modloader/tools/ImportLooseMod.csx, ROADMAP 36).

    python3 -m unittest compiler.tests.test_loose_sounds -v      (from the repo root)

Runs the native Linux LooseModCompiler (`compiler/build.sh linux-x64`) on chapter 2's vanilla
data.win with throwaway mods and reads the result back:
- a streamed SOND entry (flags without IsEmbedded, the .ogg next to data.win) replaced from
  sounds\\<name>.ogg becomes an embedded Ogg holding the winning mod's bytes;
- two mods on one sound: the later one in DR_MOD_DIRS order wins and the conflict is logged;
- a misspelled name, a file that isn't Ogg data and an unreadable file each give a WARN, and the
  build still succeeds.
Skipped when the game (env DELTARUNE_DIR), the compiler build or ffmpeg is missing.
"""
import os, shutil, struct, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
COMPILER = os.path.join(REPO, "compiler", "build", "linux-x64", "LooseModCompiler")
SCRIPT = os.path.join(REPO, "modloader", "tools", "ImportLooseMod.csx")
GAME = os.environ.get("DELTARUNE_DIR", "/mnt/steamdrive/SteamLibrary/steamapps/common/DELTARUNE")
DATA_WIN = os.path.join(GAME, "chapter2_windows", "data.win")

IS_EMBEDDED, IS_COMPRESSED = 0x1, 0x2


def chunks(d):
    out, pos = {}, 8
    while pos < len(d):
        name = d[pos:pos + 4].decode()
        size = struct.unpack_from("<I", d, pos + 4)[0]
        out[name] = (pos + 8, size)
        pos += 8 + size
    return out


def gm_string(d, off):
    return d[off:off + struct.unpack_from("<I", d, off - 4)[0]].decode()


def sounds(d):
    """name -> (flags, file, audio_id), plus the AUDO blobs."""
    ch = chunks(d)
    o, _ = ch["SOND"]
    res = {}
    for i in range(struct.unpack_from("<I", d, o)[0]):
        p = struct.unpack_from("<I", d, o + 4 + 4 * i)[0]
        name, flags, _typ, file, _eff, _vol, _pitch, _grp, aid = struct.unpack_from("<IIIIIffii", d, p)
        res[gm_string(d, name)] = (flags, gm_string(d, file) if file else "", aid)
    o, _ = ch["AUDO"]
    audo = []
    for i in range(struct.unpack_from("<I", d, o)[0]):
        p = struct.unpack_from("<I", d, o + 4 + 4 * i)[0]
        n = struct.unpack_from("<I", d, p)[0]
        audo.append(d[p + 4:p + 4 + n])
    return res, audo


def read(path):
    with open(path, "rb") as f:
        return f.read()


def tone(path, hz):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", f"sine=frequency={hz}:duration=0.5",
                    "-c:a", "libvorbis", path], check=True)


@unittest.skipUnless(os.path.isfile(DATA_WIN), "DELTARUNE not found (set DELTARUNE_DIR)")
@unittest.skipUnless(os.path.isfile(COMPILER), "run compiler/build.sh linux-x64 first")
@unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg missing")
class LooseSoundsTest(unittest.TestCase):
    TARGET = "snd_rurus_appear"   # streamed in vanilla chapter 2: flags 0x64, snd_rurus_appear.ogg

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="loose_sounds_")
        mods = os.path.join(cls.tmp, "mods")
        roots = []
        for mod in ("ModA", "ModB"):
            root = os.path.join(mods, mod, "chapter2_windows")
            os.makedirs(os.path.join(root, "sounds"))
            roots.append(root)
        a, b = (os.path.join(r, "sounds") for r in roots)
        tone(os.path.join(a, cls.TARGET + ".ogg"), 440)
        tone(os.path.join(b, cls.TARGET + ".ogg"), 880)                 # ModB is later: wins
        tone(os.path.join(b, "snd_rurus_apear.ogg"), 660)                # misspelled -> new sound + WARN
        with open(os.path.join(b, "snd_great_shine.ogg"), "wb") as f:    # not Ogg data -> WARN, skipped
            f.write(b"not an ogg file")
        os.symlink(os.path.join(cls.tmp, "missing.ogg"), os.path.join(b, "snd_revival.ogg"))  # unreadable
        cls.winner = read(os.path.join(b, cls.TARGET + ".ogg"))

        out = os.path.join(cls.tmp, "out.bin")
        open(out, "wb").close()
        env = dict(os.environ, DR_MOD_DIRS=";".join(roots), DR_OUT_PIPE=out)
        cls.proc = subprocess.run([COMPILER, "load", DATA_WIN, "-s", SCRIPT], env=env,
                                  capture_output=True, text=True, timeout=600)
        cls.log = cls.proc.stdout + cls.proc.stderr
        blob = read(out)
        cls.data = blob[8:8 + struct.unpack_from("<q", blob, 0)[0]] if len(blob) > 8 else b""
        cls.vanilla = read(DATA_WIN)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_build_succeeds(self):
        self.assertEqual(self.proc.returncode, 0, self.log)
        self.assertTrue(self.data, self.log)

    def test_streamed_sound_becomes_embedded_winner(self):
        van, _ = sounds(self.vanilla)
        self.assertFalse(van[self.TARGET][0] & (IS_EMBEDDED | IS_COMPRESSED), "fixture: target must be streamed")
        snd, audo = sounds(self.data)
        flags, _file, aid = snd[self.TARGET]
        self.assertTrue(flags & IS_COMPRESSED, hex(flags))
        self.assertEqual(audo[aid], self.winner)
        self.assertIn(f"[DR] sound {self.TARGET} (ModB): streamed sound (was {self.TARGET}.ogg next to data.win", self.log)

    def test_conflict_logged_with_winner(self):
        self.assertIn(f"[DR] sound {self.TARGET}: provided by ModA, ModB; ModB wins (load order)", self.log)

    def test_bad_inputs_warn_and_skip(self):
        self.assertIn("[DR] WARN snd_rurus_apear (ModB) is not a sound in this data.win - did you mean snd_rurus_appear?", self.log)
        self.assertIn("[DR] WARN sound snd_great_shine (ModB):", self.log)
        self.assertIn("is not an Ogg Vorbis file", self.log)
        self.assertIn("[DR] WARN sound snd_revival (ModB): can't read", self.log)
        snd, _ = sounds(self.data)
        van, _ = sounds(self.vanilla)
        for name in ("snd_great_shine", "snd_revival"):   # skipped: still the vanilla streamed entry
            self.assertEqual(snd[name][:2], van[name][:2])


if __name__ == "__main__":
    unittest.main()
