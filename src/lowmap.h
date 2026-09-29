/* This client's fixed buffers in bank 0's low RAM, below the program:
 * the family's map (mega-net PLATFORM.md) with this client's uses. All
 * reached by absolute address; sizeof is wrong on every one of them.
 *
 *   $0800-$0FFF  2048  ssh_rx, the transport's packet     transport.h
 *   $1100-$12FF   512  the disk layer's sector buffer     m65_f011 (F011_BUF_AT)
 *   $1300-$13FF   256  its BAM copy                       m65_cbmdos (BAM2_AT)
 *   $1400-$14FF   256  the current directory's path       sftpc.c
 *   $1500-$151F    32  the hypervisor's name page         m65_hyppo.h
 *   $1520-$15D1   178  the disk layer's name scratch      m65_scratch.h
 *   $15D8-$15F7    32  the drive chooser's answer         xfer.c
 *   $1600-$16FF        mega-net's trampoline              (reserved)
 *   $1700-$17FF        the crypto bank's trampoline       ckit.h
 *   $1800-$19FF        CBM DOS BAM and block buffers      m65_cbmdos.c
 *   $1A00-$1C39   570  tx, the transport's outgoing packet  transport.c
 *   $1C40-$1D3F   256  the shared name page: the parser's
 *                      entry name, and the one joined path  sftp.c, sftpc.c, xfer.c
 *   $1D40-$1F5D   542  rq, the request being built        sftp.c
 *   $1F60-$1FAF    80  the server's disconnect reason     transport.h
 *   $1FB0              the family's exit stub             m65_exit.c
 */
#ifndef LOWMAP_H
#define LOWMAP_H
#endif
