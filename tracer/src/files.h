/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The bytes of the files a report keeps: which attachment is a log and which a screenshot, a log unpacked from its .gz
   or .zip, a screenshot's kind read from its bytes and its metadata (camera, place, time, text) taken out, and text
   made valid UTF-8 for the site */
#ifndef TR_FILES_H
#define TR_FILES_H
#include "tracer.h"

typedef enum { TR_OTHER, TR_LOG, TR_IMAGE, TR_VIDEO } TrKind;
/* by the file's name (.log, .txt, .gz, .zip; .png, .jpg, .jpeg, .webp, .gif, .ppm; .mp4, .m4v, .webm) or Discord's
   content type */
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

/* a log as text: a .gz's or a .zip's (its first .log or .txt, else its first file) unpacked, any other name's bytes
   as they are, max bytes at most. 0, or -1 with why (owned) */
int tr_unpack(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why);
/* the name the text tr_unpack made of name is saved as: a .gz's without its .gz ("game.log.gz" as "game.log"), with
   .log after it when what is left is no .log or .txt; a .zip's with .log for its .zip ("logs.zip" as "logs.log"); any
   other name as it is. The site keeps the text, never the archive. Owned */
char* tr_unpacked_name(const char* name);

/* the n bytes of text with each byte that is not part of valid UTF-8 as U+FFFD, and each NUL left out; owned */
char* tr_utf8(const char* text, size_t n);

/* a log as the site shows it: without the crash report's lines that hold the game's bytes or its registers (its code
   and memory dumps), which the log summary keeps out of its own (logscan.c's dump()), and with each IPv4 address but
   the loopback's as [ip] and each e-mail address as [email]. Owned, its length in *size */
char* tr_public_log(const char* text, size_t n, size_t* size);
#endif
