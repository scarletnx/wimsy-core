/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The bytes of the files a report keeps: which attachment is a log and which a screenshot, a log unpacked from its .gz
   or .zip and the screenshots beside it in the .zip, a screenshot's kind read from its bytes and its metadata (camera,
   place, time, text) taken out, a log as the site shows it (no dumps, addresses or paths of the player's), and text
   made valid UTF-8 for the site */
#ifndef TR_FILES_H
#define TR_FILES_H
#include "tracer.h"

typedef enum { TR_OTHER, TR_LOG, TR_IMAGE, TR_VIDEO } TrKind;
/* by the file's name (.log, .txt, .jsonl, .gz, .zip; .png, .jpg, .jpeg, .webp, .gif, .ppm; .mp4, .m4v, .webm) or
   Discord's content type */
TrKind tr_kind(const char* name, const char* type);

/* the image's kind from its first bytes: "png", "jpg", "gif", "webp" or "ppm" (a binary PPM, P6, which ScarletNX's
   runtimes write their frames as); NULL for none of them */
const char* tr_image_ext(const char* data, size_t n);
/* the image's metadata taken out in place: PNG's text, time and eXIf chunks and what follows IEND, JPEG's APP1 and
   later segments (except ICC's APP2 and Adobe's APP14), comments and what follows its end, WebP's EXIF and XMP chunks,
   GIF's comments, its applications' extensions but the loop's (an editor's XMP among them) and what follows its end.
   Its new length, or 0 for an image whose structure is broken (the caller keeps none of it) */
size_t tr_strip_image(char* data, size_t n);

/* the video's kind from its first bytes: "mp4" (an ftyp box first) or "webm" (EBML, its doctype webm); NULL for
   neither */
const char* tr_video_ext(const char* data, size_t n);
/* an MP4's place, time and camera blanked in place (udta and meta whole, XMP's uuid box, and ©xyz, loci, ©mak and ©mod
   where else they are, each made a free box of zeros, so that nothing moves and the offsets into it stay right): n, or
   0 for an MP4 whose boxes are broken. A WebM stays as it is */
size_t tr_strip_video(char* data, size_t n);

/* a binary PPM (P6, 255 at most a channel, comments allowed in its header) as a PNG, which browsers show: 0, or -1 for
   a PPM that isn't whole or is bigger than 16384 pixels a side */
int tr_ppm_png(const char* data, size_t n, Buf* out);

/* a log as text: a .gz's or a .zip's unpacked, any other name's bytes as they are, max bytes at most. Of a .zip, the
   entry that holds a whole run of edenprobe (probe.h), else one a run begins in, else its first .log, .txt or .jsonl,
   else its first file that is no picture or video by its name (tr_kind's); its entries are read in looking for the run
   up to 256 MiB in all. An entry under __MACOSX/, the resource fork a Mac's zip keeps beside each file, is none of the
   player's. A zip's names are read as UTF-8 whatever the process's locale. 0, or -1 with why (owned) */
int tr_unpack(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why);

/* the UTF-8 locale a zip's names are read in, made now: before a worker shuts itself in its folder, where the locale's
   files no longer open. 0, or -1 when the system has none (C.UTF-8, en_US.UTF-8): a zip's names that are no ASCII
   are then lost, said in the reply of each zip whose screenshots are */
int tr_names_ready(void);

/* a .zip's screenshots, one at a time: its entries named as tr_kind's pictures are (.png, .jpg, .jpeg, .webp, .gif,
   .ppm), in the zip's order, but those under __MACOSX/ */
struct archive;
typedef struct {
    struct archive* a;
    char* name;  /* owned: the entry's file name, valid UTF-8, without its folders (a zip made by hand may name the
                    player's) */
    double size; /* its bytes unpacked as the zip says, -1 when it does not */
    int broken;  /* 1 once the zip stopped short of its end */
    int unnamed; /* the files passed by whose names the zip gave in no form this system reads */
} TrZipImages;
/* z opened on the n bytes of a zip: 0, or -1 (nothing owned) for bytes no zip reader opens */
int tr_zip_images(TrZipImages* z, const char* data, size_t n);
/* the next screenshot's entry: 1 with z->name and z->size, or 0 at the zip's end (z->broken when it stopped short) */
int tr_zip_next(TrZipImages* z);
/* that entry's bytes into out (cleared first), max at most: 0, 1 for an entry that unpacks to more (out empty), -1 for
   one that does not unpack */
int tr_zip_read(TrZipImages* z, size_t max, Buf* out);
void tr_zip_done(TrZipImages* z);
/* the name the text tr_unpack made of name is saved as: a .gz's without its .gz ("game.log.gz" as "game.log"), with
   .log after it when what is left is no .log, .txt or .jsonl; a .zip's with .log for its .zip ("logs.zip" as
   "logs.log"); any other name as it is. The site keeps the text, never the archive. Owned */
char* tr_unpacked_name(const char* name);

/* "[   2.25] " before a line, the time the runtime prints (its point a comma under some locales), or Eden's
   "[  12.345678] ": its length, 0 without one. As logscan.c's stamp(), it takes one space after the bracket, so a
   dump's indent is what follows */
size_t tr_stamp(const char* s, size_t n);

/* the n bytes of text with each byte that is not part of valid UTF-8 as U+FFFD, and each NUL left out; owned */
char* tr_utf8(const char* text, size_t n);

/* a log as the site shows it: without the crash report's lines that hold the game's bytes or its registers (its code
   and memory dumps), which the log summary keeps out of its own (logscan.c's dump()), with each IPv4 address but the
   loopback's as [ip] and each e-mail address as [email], and each absolute path outside a home folder as ~ and its
   file's name. Such a path is under /mnt, /media, /run/media, /Volumes (macOS), /storage or /sdcard (Android), or on a
   drive (D:\, D:/, WSL's /mnt/d/) but in its \Users (a home: snx_logscan_tilde's), \Windows or \Program Files (the
   system's, which name no one); its folders go, and with them the mount's own name (a disk's label, an account's), and
   its last name stays when it is a file's (a dot, then a letter or a digit, after its first byte):
   "/mnt/Disk/Games/Some Game.nsp (9 MB)" as "~/Some Game.nsp (9 MB)", "/media/someone" as "~". It runs before
   snx_logscan_tilde, which would make /media/<user> a home and keep the folders under it. Owned, its length in *size */
char* tr_public_log(const char* text, size_t n, size_t* size);
#endif
