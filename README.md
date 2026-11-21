# mega-sftp

Encrypted file transfer for the MEGA65. Log in to any OpenSSH server,
browse its folders, download files to a disk, and upload files from a
disk. It shares its encryption with
[mega-ssh](https://github.com/silent-command/mega-ssh), and the key you
make in either one works in both.

## Starting

Mount `SFTP.D81` and type `RUN "SFTP"`, or choose SFTP from the
[Net-Tools](https://github.com/silent-command/mega-menu) menu. It needs a
network cable.

## Keys

At the Host prompt (then Port, Auth, User and Password):

| Key | Does |
|---|---|
| RETURN | accept |
| F1 | your key: G makes one, R replaces it |
| P / I at Auth | log in with a password / with your key |
| RUN/STOP | quit to BASIC |

When a server is new, Y trusts it; any other key refuses.

In the file list:

| Key | Does |
|---|---|
| CRSR up / down | move |
| CRSR left / right | previous / next page |
| CLR/HOME | first entry |
| RETURN | open a folder, or download a file |
| P | upload a file to this folder |
| U or INST/DEL | up one folder |
| R | list again |
| D | choose where downloads go: 8, 9, or a `.D81` on the SD card |
| MEGA+F / MEGA+B | change the text / background colour |
| HELP or H | disconnect, back to the Host prompt |
| RUN/STOP | up one folder; at the top, disconnect |

RUN/STOP cancels any prompt and any transfer in progress.

## Credits

Created by Justin Cameron. Apache License 2.0; see [LICENSE](LICENSE)
and [NOTICE](NOTICE).
