# mega-sftp, for a language model picking it up

The MEGA65 SFTP client: the SSH client's transport and crypto bank, the
FTP client's browser, the SFTP v3 protocol between them. Local-only
notes: this file, NOTES.md and REQUIREMENTS.md are excluded from git in
.git/info/exclude; README.md is the user's own and is never created here.

- `python3 build.py` builds bin/SFTP.D81 and checks the stack room.
- `python3 build.py host` builds build/host/sftp_host, the whole stack
  on the Mac against real servers (tests/host/ is the machine).
- `tools/sftp_test_server.py` is an asyncssh SFTP server rooted in
  build/sftp_root; `--many N` makes a big directory.
- The crypto bank is mega-ssh's, byte for byte (SSHCRYPTO + TERM),
  built in ../mega-ssh and copied.
- Fixed low-RAM addresses are all in src/lowmap.h; sizeof is wrong on
  every one of them.
- REQUIREMENTS.md section 5 is the findings log, the family's habit.
