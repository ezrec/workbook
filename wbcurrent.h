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

STRPTR wbAbspathCurrent(struct Library *_DOSBase, CONST_STRPTR file);

// The following functions assume CurrentDir() is the target directory, and handle
// a '.icon' file correctly.
BOOL wbDeleteFromCurrent(struct Library *_DOSBase, struct Library *_IconBase, CONST_STRPTR file, BOOL only_contents);

BOOL wbCopyBumpCurrent(struct Library *_DOSBase, struct Library *_IconBase, CONST_STRPTR src_file);
BOOL wbCopyIntoCurrent(struct Library *_DOSBase, struct Library *_IconBase, BPTR src_dir, CONST_STRPTR src_file);

BOOL wbMoveIntoCurrent(struct Library *_DOSBase, struct Library *_IconBase, BPTR src_dir, CONST_STRPTR src_file);

