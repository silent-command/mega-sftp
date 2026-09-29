#!/usr/bin/env python3
"""Build driver for the MEGA65 SFTP client.

    python3 build.py            build the client and bin/SFTP.D81
    python3 build.py host       the host harness, build/host/sftp_host
    python3 build.py venv       the Python test server's environment (asyncssh)
    python3 build.py clean

The crypto bank is the SSH client's, byte for byte: this client loads
the same SSHCRYPTO and TERM images (src/ckit.c), so one copy serves
both on a shared disk. build.py builds them in ../mega-ssh when they
are missing and copies them here.

Needs llvm-mos (mos-mega65-clang), CMake, c1541 from VICE, a C99 host
compiler, and the sibling checkouts ../mega-ssh, ../mega-net and
../mega65-libc.

Overrides: CC, LLVM_MOS_DIR, MEGANET, LIBC_SRC, MEGASSH, C1541.
"""
import os, platform, re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
BIN = ROOT / "bin"
IS_WINDOWS = platform.system() == "Windows"
MEGANET = Path(os.environ.get("MEGANET", ROOT.parent / "mega-net")).resolve()
LIBC_SRC = Path(os.environ.get("LIBC_SRC", ROOT.parent / "mega65-libc")).resolve()
MEGASSH = Path(os.environ.get("MEGASSH", ROOT.parent / "mega-ssh")).resolve()
LIBC_BUILD = BUILD / "libc"


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    print(" ", " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], **kw)
    if r.returncode:
        die("error: command failed")
    return r


def find_tool(exe, roots=(), env=None):
    if env and os.environ.get(env):
        return os.environ[env]
    for root in roots:
        for sub in ("bin", ""):
            p = Path(root) / sub / (exe + (".exe" if IS_WINDOWS else ""))
            if p.is_file():
                return str(p)
    found = shutil.which(exe)
    if found:
        return found
    die(f"{exe} not found; set {env or 'PATH'}")


def check_rmw(elf):
    """ssh REQUIREMENTS.md 5.6: the compiler once decremented the wrong
    zero-page word in a loop; tools/orphan_rmw.py finds that shape."""
    if not elf.exists():
        return
    objdump = find_tool("llvm-objdump", [Path(mos_clang()).parent.parent])
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "orphan_rmw.py"), str(elf), objdump])
    if r.returncode:
        die(f"{elf.name}: the loop miscompile of ssh 5.6 is back; rewrite the loop it names")


def check_headroom(mapfile):
    """The soft stack grows down from $D000 into whatever the data left;
    the family's floor is 1 KB (gemini 5.6, mega-irc 5.16)."""
    end = 0
    for line in mapfile.read_text().splitlines():
        m = re.match(r"\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+\.(bss|noinit|data)$", line)
        if m:
            end = max(end, int(m.group(1), 16) + int(m.group(2), 16))
    room = 0xD000 - end
    print(f"  data ends at ${end:04x}: {room} bytes for the soft stack")
    if room < 1024:
        die("less than 1 KB between the program's data and $D000: the boot will crash (gemini 5.6); find the bytes")


def mos_clang():
    roots = [os.environ["LLVM_MOS_DIR"]] if os.environ.get("LLVM_MOS_DIR") else []
    roots += [Path.home() / "llvm-mos", "/opt/llvm-mos", "/usr/local/llvm-mos"]
    return find_tool("mos-mega65-clang", roots)


def host_cc():
    if os.environ.get("CC"):
        return os.environ["CC"]
    for c in ("cc", "gcc", "clang", "cl"):
        if shutil.which(c):
            return c
    die("no host C compiler found; set CC")


def ensure_libc():
    lib = LIBC_BUILD / "src" / "libmega65libc.a"
    if lib.is_file():
        return lib
    if not LIBC_SRC.is_dir():
        die(f"mega65-libc not found at {LIBC_SRC}; set LIBC_SRC")
    print("building mega65-libc for llvm-mos:")
    prefix = Path(mos_clang()).parent.parent
    run(["cmake", f"-DCMAKE_PREFIX_PATH={prefix}", "-B", LIBC_BUILD, "-S", LIBC_SRC])
    run(["cmake", "--build", LIBC_BUILD])
    return lib


def ensure_meganet():
    image = MEGANET / "build" / "m65" / "meganet.bin"
    tramp = MEGANET / "build" / "gen" / "meganet_tramp.c"
    if not (image.is_file() and tramp.is_file()):
        if not MEGANET.is_dir():
            die(f"mega-net not found at {MEGANET}; set MEGANET")
        print("building mega-net:")
        run([sys.executable, "build.py", "abi"], cwd=MEGANET)
    return image, tramp


def ensure_bank():
    """The SSH client's bank, built there so the images stay one set."""
    bank = MEGASSH / "build" / "bank"
    files = [bank / "crypto.bin", bank / "term.bin", bank / "ck_tramp.bin"]
    if not all(f.is_file() for f in files):
        if not MEGASSH.is_dir():
            die(f"mega-ssh not found at {MEGASSH}; set MEGASSH")
        print("building the crypto bank in mega-ssh:")
        run([sys.executable, "build.py", "bank"], cwd=MEGASSH)
    return files


def emit_payload(gen, tramp, image, term):
    gen.mkdir(parents=True, exist_ok=True)
    tb = tramp.read_bytes()
    (gen / "ck_payload.h").write_text(
        "/* generated by build.py: the crypto trampoline and the image sizes */\n"
        "#ifndef CK_PAYLOAD_H\n#define CK_PAYLOAD_H\n"
        f"#define CK_TRAMP_SIZE {len(tb)}\n#define CK_BIN_SIZE {image.stat().st_size}UL\n"
        f"#define CK_TERM_SIZE {term.stat().st_size}UL\n"
        "extern const unsigned char ck_tramp_bin[CK_TRAMP_SIZE];\n#endif\n")
    body = ", ".join(f"0x{x:02x}" for x in tb)
    (gen / "ck_payload.c").write_text(f'#include "ck_payload.h"\nconst unsigned char ck_tramp_bin[CK_TRAMP_SIZE] = {{ {body} }};\n')


# Named in src/sftp.ld: the window under the KERNAL, compiled apart
# (irc 5.17). NOTHING here may run before ck_boot has loaded the image:
# the disk layer was here first and the boot broke at $E720, since the
# loader of HIGH is the disk layer (5.3). Everything named is idle
# until a session: the channel, the known hosts, the listing, Hyppo.
HIGH_OBJS = ("channel.c", "hosts.c", "dirlist.c", "m65_hyppo.c")
RAM_LEN = 2 + 0xAFFF                              # the PRG's header and its whole region; the HIGH image follows


def cflags(libc_src):
    extra = os.environ.get("EXTRA_CFLAGS", "").split()
    return ["-Oz", "-I", str(libc_src / "include"), "-I", str(MEGANET / "src" / "abi"),
            "-I", str(MEGANET / "build" / "gen"), "-I", str(ROOT / "src" / "platform"),
            "-I", str(ROOT / "src"),
            "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
            # the disk layer's two buffers in low RAM the ROM's reset rebuilds
            # on exit (m65_exit.c leaves through that reset; ssh 5.35)
            "-DF011_BUF_AT=0x1100", "-DBAM2_AT=0x1300"] + extra


def build_client():
    clang = mos_clang(); lib = ensure_libc(); image, tramp = ensure_meganet()
    crypto, term, ck_tramp = ensure_bank()
    BIN.mkdir(exist_ok=True)
    gen = BUILD / "gen"
    emit_payload(gen, ck_tramp, crypto, term)
    srcs = sorted(str(p) for d in (ROOT / "src", ROOT / "src" / "platform")
                  for p in d.glob("*.c") if p.name not in HIGH_OBJS)
    srcs += sorted(str(p) for p in (ROOT / "src" / "platform").glob("*.S"))
    hi_objs = []
    for name in HIGH_OBJS:
        src = next(d / name for d in (ROOT / "src", ROOT / "src" / "platform") if (d / name).exists())
        obj = gen / (src.stem + ".o")
        run([clang] + cflags(LIBC_SRC) + ["-I", str(gen), "-fno-lto", "-c", str(src), "-o", str(obj)])
        hi_objs.append(str(obj))
    prg = BIN / "sftp.prg"
    out = BIN / "sftp.bin"
    print("client:")
    run([clang] + cflags(LIBC_SRC) + ["-I", str(gen), "-T", str(ROOT / "src" / "sftp.ld")] + srcs + hi_objs +
        [str(gen / "ck_payload.c"), str(tramp), str(MEGANET / "src" / "abi" / "meganet_vectors.c"), str(lib),
         f"-Wl,-Map={BIN / 'sftp.map'}", "-o", str(out)])
    data = out.read_bytes()
    if len(data) < RAM_LEN:
        die("the client image is shorter than its region; is OUTPUT_FORMAT FULL(ram)?")
    prg.write_bytes(data[:RAM_LEN].rstrip(b"\0"))
    high = BIN / "high"
    high.write_bytes(data[RAM_LEN:].rstrip(b"\0") or b"\0")
    print(f"  {prg.name}: {prg.stat().st_size} bytes; {high.name}: {high.stat().st_size} bytes (for $E000)")
    if high.stat().st_size < 1000:
        die("the HIGH image is nearly empty: sftp.ld's patterns matched nothing (gemini 5.8)")
    check_rmw(out.with_suffix(".bin.elf"))
    check_headroom(BIN / "sftp.map")
    c1541 = find_tool("c1541", env="C1541")
    d81 = BIN / "SFTP.D81"
    shutil.copy(image, BIN / "meganet")
    shutil.copy(crypto, BIN / "sshcrypto")
    shutil.copy(term, BIN / "term")
    if d81.exists():
        d81.unlink()
    empty = BUILD / "empty.seq"                     # an empty IDENTITY ships on the disk (ssh 5.26)
    empty.write_bytes(b"")
    run([c1541, "-format", "sftp,sf", "d81", d81, "-write", prg, "sftp", "-write", BIN / "meganet", "meganet",
         "-write", BIN / "sshcrypto", "sshcrypto", "-write", BIN / "term", "term", "-write", high, "high",
         "-write", empty, "identity,s"], stdout=subprocess.DEVNULL)
    run([c1541, "-attach", d81, "-dir"])
    return 0


def build_host():
    """tests/sftp_host.c: the SFTP layer over the unchanged transport,
    against real servers on this machine (tests/host/ is the machine)."""
    out = BUILD / "host"; out.mkdir(parents=True, exist_ok=True)
    exe = out / ("sftp_host" + (".exe" if IS_WINDOWS else ""))
    crypto = MEGASSH / "src" / "crypto"
    srcs = [ROOT / "tests" / "sftp_host.c", ROOT / "tests" / "host" / "host_kit.c",
            ROOT / "src" / "transport.c", ROOT / "src" / "wire.c", ROOT / "src" / "channel.c", ROOT / "src" / "sftp.c"]
    srcs += [crypto / n for n in ("chacha20.c", "curve25519.c", "poly1305.c", "sha256.c", "sha512.c")]
    run([host_cc(), "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
         "-I", ROOT / "tests" / "host", "-I", ROOT / "src", "-I", crypto] + srcs + ["-o", exe])
    print(f"host harness: {exe.relative_to(ROOT)}")
    return 0


def build_venv():
    venv = BUILD / "venv"
    py = venv / ("Scripts/python.exe" if IS_WINDOWS else "bin/python")
    if not py.is_file():
        run([sys.executable, "-m", "venv", str(venv)])
    run([str(py), "-m", "pip", "install", "-q", "asyncssh"])
    print("venv ready:", py)
    return 0


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "client"
    if target == "client":
        return build_client()
    if target == "host":
        return build_host()
    if target == "venv":
        return build_venv()
    if target == "clean":
        shutil.rmtree(BUILD, ignore_errors=True); shutil.rmtree(BIN, ignore_errors=True); return 0
    print(__doc__); die(f"unknown target '{target}'")


if __name__ == "__main__":
    sys.exit(main())
