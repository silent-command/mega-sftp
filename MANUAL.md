# MEGA65 SFTP Client

Encrypted file transfer for the MEGA65 in native C, over SSH: browse a
server's file system, fetch files onto a disk, send files back. Any
machine running OpenSSH answers it with no server setup, which is most
Linux boxes, NAS devices, and a Mac with Remote Login switched on.
Version 0.1.0.

## What is on the disk

`SFTP.D81` carries everything the client needs:

| File        | What it is                                                    |
|-------------|---------------------------------------------------------------|
| `sftp`      | the client; `RUN "SFTP"`                                      |
| `meganet`   | the mega-net TCP/IP stack, loaded into upper memory at start  |
| `sshcrypto` | the crypto bank: keys, ciphers, signatures (the SSH client's) |
| `term`      | that bank's second half                                       |
| `sftphigh`  | the client's own second image                                 |
| `identity`  | your Ed25519 key, once one is generated                       |

The client writes `knownhosts` and `sshauth` to the disk as you use it.
A network cable is needed; mega-net takes a DHCP lease at start.

## Connecting

The first screen asks for the host, the port (22 unless the server
says otherwise), the login method, and the user name. RETURN accepts
what a field already shows. RUN/STOP at the Host prompt quits to BASIC.

Two ways to log in:

- **p, a password.** Typed each time, shown as asterisks, never stored.
- **i, your identity.** An Ed25519 key pair made on the MEGA65 and kept
  on the disk. F1 at the Host prompt shows the public key the way
  OpenSSH writes it; put that line in `~/.ssh/authorized_keys` on the
  server and the client logs in without a password. F1 also generates
  the key the first time, from about ten seconds of gathered noise.

The first connection to a host shows the server's Ed25519 key as a
SHA-256 fingerprint and asks before trusting it. Y remembers the host
in `knownhosts`; after that, a server whose key has changed gets a
warning instead. The key exchange takes about half a minute on the
MEGA65, most of it verifying the server's signature.

## The browser

The listing shows directories first, then files, each with its size.
The current path is in the title row.

| Key           | What it does                                             |
|---------------|----------------------------------------------------------|
| CRSR up/down  | move the selection                                       |
| CRSR left/right | previous / next page                                   |
| HOME          | the first entry                                          |
| RETURN        | open a directory, or fetch the selected file             |
| U or DEL      | up one directory                                         |
| R             | list again                                               |
| P             | send a file from a disk to this directory                |
| D             | choose where downloads go                                |
| MEGA-F / MEGA-B | cycle the text / background color                      |
| HELP          | end the session, back to the Host prompt                 |
| RUN/STOP      | up one directory; at the root, ends the session          |

A listing keeps up to 110 entries and says when a longer directory was
cut. A symbolic link is opened as a directory first and fetched as a
file if the server refuses.

**Fetching**: RETURN on a file asks for the name to use on the disk
(16 characters, suggested from the server's name) and the file type,
SEQ for data and text or PRG for a program; a name ending `.PRG`
suggests PRG. An existing name asks before overwriting. RUN/STOP
cancels a running transfer and removes the partial file.

**Sending**: P asks which drive, the file's name on that disk, and the
name to give it on the server, suggested from the disk name. The file
lands in the directory being viewed.

**Drives**: downloads go to unit 8 or unit 9. At the D prompt, `8` or
`9` picks the unit, and any other answer names a `.D81` on the SD card,
which is attached to unit 9 and used from then on.

Transfers run at roughly 1 to 2 KB per second; the arithmetic that
encrypts every packet is the cost of the encryption.

## Building it

Needs llvm-mos (`mos-mega65-clang`), the mega65-libc, mega-net and
mega-ssh checkouts beside this one, VICE's `c1541`, and Python 3. The
crypto bank is built in mega-ssh and shared byte for byte.

    python3 build.py            the client and bin/SFTP.D81
    python3 build.py host       the same protocol code as a command-line
                                client on this machine, for testing
    python3 tools/deploy.py     the disk onto the card, keeping the
                                identity and known hosts already there
    python3 tools/sftp_test_server.py   a local SFTP server to test against

## License

Apache 2.0.
