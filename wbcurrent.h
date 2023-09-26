// Copyright 2023, Jason S. McMullan <jason.mcmullan@gmail.com>
//
// This code licensed under the MIT License, as follows:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the “Software”), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <proto/exec.h>
#include <proto/dos.h>

#ifdef __AROS__
#include "workbook_aros.h"
#else
#include "workbook_vbcc.h"
#endif

#if DEBUG
CONST_STRPTR _sCURRDIR(struct Library *_DOSBase);
#define sCURRDIR() _sCURRDIR(DOSBase)
CONST_STRPTR _sLOCKNAME(struct Library *_DOSBase, BPTR lock);
#define sLOCKNAME(lock) _sLOCKNAME(DOSBase, lock)
#endif

STRPTR _wbAbspathLock(struct Library *_DOSBase, BPTR lock);
#define wbAbspathLock(lock) _wbAbspathLock(DOSBase, lock);

STRPTR _wbAbspathCurrent(struct Library *_DOSBase, CONST_STRPTR file);
#define wbAbspathCurrent(file) _wbAbspathCurrent(DOSBase, file);

// The following functions assume CurrentDir() is the target directory, and handle
// a '.icon' file correctly.
BOOL _wbDeleteFromCurrent(struct Library *_DOSBase, struct Library *_IconBase, CONST_STRPTR file, BOOL only_contents);
#define wbDeleteFromCurrent(file, only_contents) _wbDeleteFromCurrent(DOSBase, IconBase, file, only_contents)

BOOL _wbCopyBumpCurrent(struct Library *_DOSBase, struct Library *_IconBase, CONST_STRPTR src_file);
#define wbCopyBumpCurrent(src_file) _wbCopyBumpCurrent(DOSBase, IconBase, src_file)

BOOL _wbCopyIntoCurrentAt(struct Library *_DOSBase, struct Library *_IconBase, BPTR src_dir, CONST_STRPTR src_file, LONG targetX, LONG targetY);
#define wbCopyIntoCurrentAt(src_dir, src_file, targetX, targetY) _wbCopyIntoCurrentAt(DOSBase, IconBase, src_dir, src_file, targetX, targetY)
#define wbCopyIntoCurrent(src_dir, src_file) _wbCopyIntoCurrentAt(DOSBase, IconBase, src_dir, src_file, (LONG)NO_ICON_POSITION, (LONG)NO_ICON_POSITION)
BOOL _wbMoveIntoCurrentAt(struct Library *_DOSBase, struct Library *_IconBase, BPTR src_dir, CONST_STRPTR src_file, LONG targetX, LONG targetY);
#define wbMoveIntoCurrentAt(src_dir, src_file, targetX, targetY) _wbMoveIntoCurrentAt(DOSBase, IconBase, src_dir, src_file, targetX, targetY)
#define wbMoveIntoCurrent(src_dir, src_file) _wbMoveIntoCurrentAt(DOSBase, IconBase, src_dir, src_file, (LONG)NO_ICON_POSITION, (LONG)NO_ICON_POSITION)

BOOL _wbDropOntoCurrentAt(struct Library *_DOSBase, struct Library *_IconBase, struct Library *_UtilityBase, struct TagItem *tags, LONG targetX, LONG targetY);
#define wbDropOntoCurrentAt(tags, targetX, targetY) _wbDropOntoCurrentAt(DOSBase, IconBase, UtilityBase, tags, targetX, targetY)
#define wbDropOntoCurrent(tags) _wbDropOntoCurrentAt(DOSBase, IconBase, UtilityBase, tags, (LONG)NO_ICON_POSITION, (LONG)NO_ICON_POSITION)

void _wbBackdropLoadCurrent(struct Library *_DOSBase, struct List *backdrops);
#define wbBackdropLoadCurrent(backdrops) _wbBackdropLoadCurrent(DOSBase, backdrops)
BOOL _wbBackdropSaveCurrent(struct Library *_DOSBase, struct List *backdrops);
#define wbBackdropSaveCurrent(backdrops) _wbBackdropSaveCurrent(DOSBase, backdrops)
void wbBackdropFree(struct List *backdrops);

BOOL _wbBackdropContains(struct Library *_DOSBase, struct List *backdrops, BPTR lock);
#define wbBackdropContains(backdrops, lock) _wbBackdropContains(DOSBase, backdrops, lock)
BOOL _wbBackdropAdd(struct Library *_DOSBase, struct List *backdrops, BPTR lock);
#define wbBackdropAdd(backdrops, lock) _wbBackdropAdd(DOSBase, backdrops, lock)
BOOL _wbBackdropDel(struct Library *_DOSBase, struct List *backdrops, BPTR lock);
#define wbBackdropDel(backdrops, lock) _wbBackdropDel(DOSBase, backdrops, lock)
BOOL _wbBackdropNext(struct Library *_DOSBase, struct List *backdrops, BPTR lock, BPTR *next_ptr);
#define wbBackdropNext(backdrops, lock, next_ptr) _wbBackdropNext(DOSBase, backdrops, lock, next_ptr)
