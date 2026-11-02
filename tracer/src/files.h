/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The bytes of the files a report keeps: which attachment is a log and which a screenshot, a log unpacked from its .gz
   or .zip, a screenshot's kind read from its bytes and its metadata (camera, place, time, text) taken out, and text
   made valid UTF-8 for the site */
#ifndef TR_FILES_H
#define TR_FILES_H
#include "tracer.h"

typedef enum { TR_OTHER, TR_LOG, TR_IMAGE } TrKind;
/* by the file's name (.log, .txt, .gz, .zip; .png, .jpg, .jpeg, .webp, .gif) or Discord's content type */
TrKind tr_kind(const char* name, const char* type);

/* the image's kind from its first bytes: "png", "jpg", "gif" or "webp"; NULL for none of them */
const char* tr_image_ext(const char* data, size_t n);
/* the image's metadata taken out in place: PNG's text, time and eXIf chunks, JPEG's APP1 and later segments (except
   ICC's APP2 and Adobe's APP14) and comments, WebP's EXIF and XMP chunks. Its new length, or 0 for an image whose
   structure is broken (the caller keeps none of it). A GIF stays as it is */
size_t tr_strip_image(char* data, size_t n);

/* a log as text: a .gz's or a .zip's (its first .log or .txt, else its first file) unpacked, any other name's bytes
   as they are, max bytes at most. 0, or -1 with why (owned) */
int tr_unpack(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why);

/* the n bytes of text with each byte that is not part of valid UTF-8 as U+FFFD, and each NUL left out; owned */
char* tr_utf8(const char* text, size_t n);
#endif
