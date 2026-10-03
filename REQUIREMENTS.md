# MEGA65 SFTP client

## 1. What it is

Encrypted file transfer to the servers people actually run: OpenSSH
turns the SFTP subsystem on by default, so a Linux box, a NAS or a Mac
with Remote Login answers this client with no server setup. FTPS lost
that argument (2026-09-29, with the user): almost nobody runs it, and
its per-connection TLS handshakes would cost half a minute each on this
machine. SFTP rides one SSH session for everything.

## 2. The shape

The SSH client's transport, crypto bank (SSHCRYPTO and TERM, byte for
byte), known hosts, identity and login; the FTP client's browser,
drive chooser and Hyppo attach; SFTP version 3 between them, one
request outstanding, replies parsed as they stream so a listing never
needs to fit in memory. The directory lives in bank 1 at $1C000,
sorted as it arrives. Version 0.1 scope, decided when the first link
came out 17 KB over: browse, get, put (a typed name), the identity and
host-key screens, the drive chooser with attach-by-name; bookmarks,
the upload picker, delete/mkdir/rename wait for bytes (the protocol
half of all three is in sftp.c and tested on the host).

## 5. Findings

### 5.1 The host harness came first (2026-09-29)

tests/host/ is the machine as three stubs: PEEK($D7FA) from the Mac's
clock, mega-net's sockets over BSD sockets, the crypto bank on the same
primitives it is built from. The unchanged transport plus the new SFTP
layer ran against asyncssh and against real OpenSSH before any of it
was compiled for the MEGA65: pwd, a 300-file and a 924-file listing
(diffed against ls, identical), 200 KB up and down byte-identical,
stat, mkdir, rename, rm, rmdir, and the error words for a missing file
and a full directory. sftp_host also serves as the protocol reference:
IP PORT USER PASSWORD (or seed:HEX64 for key login) COMMAND.

### 5.2 The first link was 17 KB over, and where that went (2026-09-29)

Browser plus transport is simply more program than either parent. The
path to fitting, in order of what it bought: the IRC client's second
image under the KERNAL (HIGH, loaded by ck_boot: the disk layer, the
known hosts, Hyppo -- 6.2 KB out of the main region); dropping
bookmarks, the upload picker, delete/mkdir/rename and the stat-follow
of links from v0.1 (~5 KB); the low-RAM map in lowmap.h -- ssh_rx at
$0800 as the SSH client has it, tx, rq, the name page, the path, the
disk-layer scratch, the disconnect reason (~2.5 KB of .bss); the
32-bit division out of ui_put_ulong (subtract powers of ten). A
noinline pass on the big screen helpers was tried and REVERTED: LTO
was already right, and it cost 686 bytes. Links are opened as
directories first and fetched as files when that refuses, which is the
FTP client's CWD-else-RETR shape and needs no stat. Result:
sftp.prg 41,987 + HIGH 6,201, stack room 1,179.

### 5.3 Nothing in HIGH may run before HIGH is loaded (2026-09-29)

The first boot broke into the ROM monitor at $E720: the disk layer was
in the HIGH image, and the code that loads HIGH from the disk is the
disk layer. The IRC client never hit this because its window holds
leaf modules. The rule for HIGH_OBJS, now written next to it: nothing
that runs before ck_boot returns. The window holds the channel, the
known hosts, the listing and Hyppo instead, all idle until a session.

### 5.4 sizeof on a fixed address is 2, and it can delete your feature (2026-09-29)

Two of them, after the lowmap move. The drive answer's prompt offered
1 character (sizeof pointer - 1). Worse, xfer_put's room check became
`base + 17 > 2`, always true: the branch always returned, and LTO
deleted the entire unreachable upload path -- the build shrank 1.8 KB
and looked healthier while put was a two-line stub that said "the
directory's path is too long". The stack-room gain from a cut is only
real when the feature still runs. Every fixed buffer now has its cap
next to it or in lowmap.h; grep for sizeof before trusting a build.

### 5.5 First hardware session (2026-09-29)

Against the asyncssh test server on the Mac: the host-key screen's
fingerprint matched the server's key file digit for digit, password
login, the login directory listed; the 300-file directory streamed
through and the client kept its 110 and said "(cut: too many)"; a
100 KB random file down to unit 8 (394 blocks) and back up under a new
name, both compared byte-identical on the Mac; KNOWNHOSTS on the boot
disk after the Y. Upload runs at about 1.8 KB/s (56 s for 100 KB);
the download is slower, minutes for the same file, mostly the
one-outstanding-request round trips plus the byte-at-a-time disk
write. save_data hit the ssh 5.6 loop miscompile and is a pointer walk
now; build.py's checker caught it before the machine did.
