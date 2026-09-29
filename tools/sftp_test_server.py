#!/usr/bin/env python3
"""An SFTP server for testing the MEGA65 client.

    python3 tools/sftp_test_server.py [--port 2230] [--root DIR] [--many N]

User `mega65`, password `mega65`, or any public key. The client sees DIR
(build/sftp_root by default, created if missing) as its whole file
system, "/" being DIR: nothing outside it can be read or written. The
host key is an Ed25519 key generated once into build/test_host_key.
Algorithms are limited to what the client offers, so a mismatch shows
up here first. --many N fills a directory "many" with N files, for a
listing that spans several READDIR replies and many channel packets.

Needs asyncssh: `python3 build.py venv` makes build/venv with it."""
import argparse, asyncio, os, sys
from pathlib import Path
import asyncssh

ROOT = Path(__file__).resolve().parent.parent
KEY = ROOT / "build" / "test_host_key"

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=2230)
ap.add_argument("--root", default=str(ROOT / "build" / "sftp_root"))
ap.add_argument("--many", type=int, default=0)
args = ap.parse_args()

top = Path(args.root).resolve()
top.mkdir(parents=True, exist_ok=True)
if args.many:
    many = top / "many"
    many.mkdir(exist_ok=True)
    for i in range(args.many):
        (many / f"file-with-a-longish-name-{i:04d}.txt").write_text(f"file {i}\n" * (i % 7 + 1))
if not KEY.exists():
    KEY.parent.mkdir(parents=True, exist_ok=True)
    asyncssh.generate_private_key("ssh-ed25519").write_private_key(str(KEY))


class Server(asyncssh.SSHServer):
    def connection_made(self, conn):
        print("connection from", conn.get_extra_info("peername")[0], flush=True)

    def begin_auth(self, username):
        return True

    def password_auth_supported(self):
        return True

    def validate_password(self, username, password):
        ok = username == "mega65" and password == "mega65"
        print("password", username, "ok" if ok else "REFUSED", flush=True)
        return ok

    def public_key_auth_supported(self):
        return True

    def validate_public_key(self, username, key):
        print("public key", username, key.get_fingerprint(), flush=True)
        return True


class Rooted(asyncssh.SFTPServer):
    def __init__(self, chan):
        super().__init__(chan, chroot=str(top))


async def main():
    await asyncssh.create_server(Server, "", args.port, server_host_keys=[str(KEY)],
                                 sftp_factory=Rooted, allow_scp=False,
                                 kex_algs=["curve25519-sha256", "curve25519-sha256@libssh.org"],
                                 encryption_algs=["chacha20-poly1305@openssh.com"],
                                 server_version="mega65sftptest")
    print("listening on", args.port, "root", top, flush=True)
    await asyncio.Event().wait()

asyncio.run(main())
