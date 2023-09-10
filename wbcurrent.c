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

#include <limits.h>
#include <stdio.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/utility.h>

#ifdef __AROS__
#include <exec/rawfmt.h>
#endif

#include "wbcurrent.h"

extern struct ExecBase *SysBase;

// Return the absolute path of a lock.
// Caller must FreeVec() the result.
STRPTR _wbAbspathLock(struct Library *DOSBase, BPTR lock)
{
    STRPTR buff;
    STRPTR path = NULL;

    buff = AllocVec(PATH_MAX, MEMF_ANY);
    if (buff) {
         if (NameFromLock(lock, buff, PATH_MAX)) {
            path = StrDup(buff);
        }
        FreeVec(buff);
    }

    return path;
}

// Return the absolute path of CurrentDir() and a file.
// Caller must FreeVec() the result.
STRPTR _wbAbspathCurrent(struct Library *DOSBase, CONST_STRPTR file)
{
    STRPTR buff;
    STRPTR path = NULL;

    buff = AllocVec(PATH_MAX, MEMF_ANY);
    if (buff) {
        BPTR pwd = CurrentDir(BNULL);
        CurrentDir(pwd);
        if (NameFromLock(pwd, buff, PATH_MAX - STRLEN(file) - 1 - 1)) {
            AddPart(buff, file, PATH_MAX);
            path = StrDup(buff);
        }
        FreeVec(buff);
    }

    return path;
}

// Forward reference.
static BOOL wbDeleteThisCurrent(struct Library *DOSBase, CONST_STRPTR file, struct FileInfoBlock *fib);

// Delete the contents of a directory.
// NOTE: 'fib' must _already_ have been Examine(dir, fib)'d !!!
static BOOL wbDeleteInto(struct Library *DOSBase, BPTR dir, struct FileInfoBlock *fib)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    BPTR olddir = CurrentDir(dir);
    BOOL ok = TRUE;
    LONG err = 0;

    while (ExNext(dir, fib)) {
        STRPTR file = StrDup(fib->fib_FileName);
        if (file) {
            ok = wbDeleteThisCurrent(DOSBase, file, fib);
            FreeVec(file);
        } else {
            ok = FALSE;
        }

        if (!ok) {
            break;
        }

        // Re-Examine the directory, as we may have disturbed the directory's ExNext() chains.
        ok = Examine(dir, fib);
        if (!ok) {
            break;
        }
    }

    err = IoErr();
    CurrentDir(olddir);

    SetIoErr(err);
    return ok;
}

// Delete a single file or directory.
static BOOL wbDeleteThisCurrent(struct Library *DOSBase, CONST_STRPTR file, struct FileInfoBlock *fib)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    BOOL ok;
    LONG err = 0;

    // Determine if directory or file.
    BPTR lock = Lock(file, SHARED_LOCK);
    if (lock == BNULL) {
        err = IoErr();
        ok = FALSE;
    } else {
        ok = Examine(lock, fib);
        if (ok) {
            if (fib->fib_DirEntryType >= 0) {
                // A directory - clear it out!
                ok = wbDeleteInto(DOSBase, lock, fib);
            }
        }
        err = IoErr();
        UnLock(lock);
        if (ok) {
            // Just a file (or a now empty directory);
            ok = DeleteFile(file);
            err = IoErr();
        }
    }

    SetIoErr(err);
    return ok;
}

// Delete a file, a directory, or just the contents of a directory.
BOOL _wbDeleteFromCurrent(struct Library *DOSBase, struct Library *IconBase, CONST_STRPTR file, BOOL only_contents)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct FileInfoBlock *fib = AllocDosObjectTags(DOS_FIB, TAG_END);
    if (!fib) {
        return FALSE;
    }

    BOOL ok = FALSE;
    LONG err = 0;

    if (only_contents) {
        BPTR lock = Lock(file, SHARED_LOCK);
        if (lock != BNULL) {
            ok = Examine(lock, fib);
            if (fib->fib_DirEntryType < 0) {
                // We can't "delete only contents" a file.
                SetIoErr(ERROR_OBJECT_WRONG_TYPE);
                ok = FALSE;
            }
            if (ok) {
                ok = wbDeleteInto(DOSBase, lock, fib);
            }
            err = IoErr();
            UnLock(lock);
        }
    } else {
        ok = wbDeleteThisCurrent(DOSBase, file, fib);
        err = IoErr();

        // Lastly, delete the icon.
        if (ok) {
            DeleteDiskObject((STRPTR)file);
        }
    }

    FreeDosObject(DOS_FIB, fib);

    SetIoErr(err);
    return ok;
}

#define WBBUMP_LENGTH_MAX   19  // Copy_2147483648_of_... (copy 2^31)

// Get the next 'Copy_of_...' name for a file.
// Examples:
//
// 'FooBar' => 'Copy_of_FooBar'
// 'Copy_of_FooBar' => 'Copy_2_of_FooBar'
// 'Qux' [and 'Copy_of_Qux' and 'Copy_2_of_Qux' present] => 'Copy_3_of_Qux'
// 'Copy_999_of_Xyyzy' => 'Copy_1000_of_Xyyzy'
// 'Copy of Some' => 'Copy_of_Copy of Some'
//
// Enhanced version of 'icon.library/BumpRevision' that can handle input names up to FILENAME_MAX - 19 in length.
static BOOL wbBumpRevisionCurrent(struct Library *DOSBase, CONST_STRPTR oldname, STRPTR newname)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    // Determine the current index.
    ULONG index = 0;
    enum { PREFIX, NUMBER, SUFFIX, DONE } state;
    state = PREFIX;
    int substate = 0;
    CONST_STRPTR prefix = "copy";
    CONST_STRPTR suffix = "of";
    for (CONST_STRPTR cp = oldname; state != DONE && *cp != 0; cp++, substate++) {
        switch (state) {
        case PREFIX:
            if (prefix[substate] == 0 && *cp == '_') {
                state = NUMBER;
            } else if ((prefix[substate]|0x20) != ((*cp)|0x20)) {
                state = DONE;
            }
            break;
        case NUMBER:
            if (((*cp)|0x20) == (suffix[0]|0x20)) {
                cp--;
                state = SUFFIX;
                substate = -1;
                index = 1;
            } else if (*cp == '_' && index > 1) {
                state = SUFFIX;
                substate = -1;
            } else if (*cp >= '0' && *cp <= '9') {
                index *= 10;
                index += (*cp) - '0';
            } else {
                state = DONE;
                index = 0;
            }
            break;
        case SUFFIX:
            if (suffix[substate] == 0 && *cp == '_' && cp[1] != 0) {
                oldname = &cp[1];
                state = DONE;
            } else if ((suffix[substate]|0x20) != ((*cp) | 0x20)) {
                index = 0;
                state = DONE;
            }
            break;
        case DONE:
            break;
        }
    }

    for (index++; index != 0; index++) {
       IPTR val[] = {(IPTR)index, (IPTR)oldname };
       int vindex = 0;
       const char *template = "Copy_%ld_of_%s";
       if (index == 1) {
           template = "Copy_of_%s";
           vindex = 1;
       }
       ULONG count = 0;
       RawDoFmt(template, (RAWARG)&val[vindex], RAWFMTFUNC_COUNT, &count);
       if (count >= FILENAME_MAX) {
           index = 0;
           break;
       }
       RawDoFmt(template, (RAWARG)&val[vindex], RAWFMTFUNC_STRING, newname);

       BPTR lock = Lock(newname, SHARED_LOCK);
       if (lock == BNULL) {
           // Yeah! File doesn't (yet) exist!
           D(bug("%s: Copy %ld available: %s\n", __func__, (IPTR)index, newname));
           break;
       }
       UnLock(lock);
    }

    return index != 0;
}

static inline CONST_STRPTR _sCURRDIR(struct Library *DOSBase) {
    BPTR pwd = CurrentDir(BNULL);
    CurrentDir(pwd);

    static char path[PATH_MAX];
    NameFromLock(pwd, path, sizeof(path));
    return path;
}
#define sCURRDIR() _sCURRDIR(DOSBase)

static inline CONST_STRPTR _sLOCKNAME(struct Library *DOSBase, BPTR lock) {
    static char path[PATH_MAX];
    NameFromLock(lock, path, sizeof(path));
    return path;
}
#define sLOCKNAME(lock) _sLOCKNAME(DOSBase, lock)


// Copy a single file/directory to here.
// Does NOT take special care for .icon files!
// NOTE: This routine _eats_ src_lock!
static BOOL wbCopyLockCurrent(struct Library *DOSBase, CONST_STRPTR dst_file, BPTR src_lock)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    const size_t buff_size = 4096;
    BOOL ok = FALSE;
    LONG err = 0;

    struct FileInfoBlock *fib = AllocDosObjectTags(DOS_FIB, TAG_END);
    if (!fib) {
        D(bug("%s: Can't allocate FIB\n", __func__));
        UnLock(src_lock);
        return FALSE;
    }

    // Examine the lock to see what it is.
    ok = Examine(src_lock, fib);
    if (ok) {
        // Cache attributes we may need later.
        LONG protection = fib->fib_Protection;

        ok = FALSE;
        if (fib->fib_DirEntryType>=0) {
            // Directory copies.
            BPTR dst_lock = CreateDir(dst_file);
            err = IoErr();
            if (dst_lock != BNULL) {
                // 'cd' into destination dir
                BPTR pwd = CurrentDir(dst_lock);
                // Copy all the files in src_lock to dst_lock
                ok = TRUE;
                while (ExNext(src_lock, fib)) {
                    BPTR this_lock;
                    CurrentDir(src_lock);
                    this_lock = Lock(fib->fib_FileName, SHARED_LOCK);
                    CurrentDir(dst_lock);

                    D(if (this_lock == BNULL) bug("%s: Can't lock %s|%s\n", sCURRDIR(), fib->fib_FileName));
                    err = IoErr();
                    if (this_lock != BNULL) {
                        ok = wbCopyLockCurrent(DOSBase, fib->fib_FileName, this_lock);
                        err = IoErr();
                        if (!ok) {
                            break;
                        }
                    }
                }
                CurrentDir(pwd);
                UnLock(dst_lock);
            } else {
                D(bug("%s: CreateDir(%s): %ld\n", __func__, dst_file, (IPTR)err));
            }
            UnLock(src_lock);
        } else {
            // Copy the old file to the new location
            BYTE *buff = AllocVec(buff_size, MEMF_ANY);
            if (!buff) {
                D(bug("%s: AllocVec(%ld, MEMF_ANY) = NULL\n", __func__, buff_size));
                UnLock(src_lock);
                err = ERROR_NO_FREE_STORE;
                ok = FALSE;
            } else {
                BPTR srcfh = OpenFromLock(src_lock);
                err = IoErr();
                if (srcfh == BNULL) {
                    D(bug("%s: OpenFromLock(%s): %ld\n", __func__, sLOCKNAME(src_lock), IoErr()));
                    if (err != 0) {
                        // WORKAROUND: Some AROS kernels have a bug where OpenFromLock() succeeds, yet
                        //             returns an BNULL filehandle! If (fh = BNULL, IoErr() == 0), don't UnLock()!
                        UnLock(src_lock);
                    }
                } else {
                    BPTR dst_lock = Lock(dst_file, SHARED_LOCK);
                    if (dst_lock != BNULL) {
                        // Don't copy on top of an existing object.
                        D(bug("%s: '%s' already exists in target\n", __func__, dst_file));
                        UnLock(dst_lock);
                        SetIoErr(ERROR_OBJECT_EXISTS);
                        ok = FALSE;
                    } else {
                        BPTR dstfh = Open(dst_file, MODE_NEWFILE);
                        err = IoErr();
                        ok = (dstfh != BNULL);
                        D(if (!ok) bug("%s: Open: %ld\n", __func__, IoErr()));
                        if (ok) {
                            LONG bytes;
                            while ((bytes = Read(srcfh, buff, buff_size)) > 0) {
                                LONG copied = Write(dstfh, buff, buff_size);
                                if (copied < 0) {
                                    err = IoErr();
                                    break;
                                }
                            }

                            // Did we copy everything?
                            ok = (bytes == 0);
                            D(if (!ok) bug("%s: Copy: %ld\n", __func__, IoErr()));
                            Close(dstfh);
                            if (!ok) {
                                // Clean up our mess.
                                DeleteFile(dst_file);
                            }
                        }
                    }
                    Close(srcfh);
                }
                FreeVec(buff);
            }
        }

        // Copy protection.
        if (ok) {
            ok = SetProtection(dst_file, protection);
            err = IoErr();
        }
    } else {
        UnLock(src_lock);
    }

    FreeDosObject(DOS_FIB, fib);

    SetIoErr(err);

    D(if (!ok) bug("%s: %s %s, exit: %ld\n", __func__, sCURRDIR(), dst_file, (IPTR)err));

    return ok;
}

// Copy into the same directory, bumping the 'Copy_of_...' prefix as needed.
BOOL _wbCopyBumpCurrent(struct Library *DOSBase, struct Library *IconBase, CONST_STRPTR src_file)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    BOOL ok = TRUE;
    LONG err = 0;

    // Compute the new name
    char *dst_file = AllocVec(FILENAME_MAX, MEMF_ANY);
    if (!dst_file) {
        err = ERROR_NO_FREE_STORE;
        return FALSE;
    }

    if (!wbBumpRevisionCurrent(DOSBase, src_file, dst_file)) {
        D(bug("%s: Can't bump version of '%s'\n", __func__, src_file));
        FreeVec(dst_file);
        return FALSE;
    }

    // Is there a DiskObject to copy?
    // Copy it, but clear it's positioning information.
    struct DiskObject *diskobject = GetDiskObject(src_file);
    if (diskobject != NULL) {
        // Clear positioning information.
        diskobject->do_CurrentX = (LONG)NO_ICON_POSITION;
        diskobject->do_CurrentY = (LONG)NO_ICON_POSITION;
        // Write out the new disk object.
        ok = PutDiskObject(dst_file, diskobject);
        err = IoErr();
        FreeDiskObject(diskobject);
    }

    if (ok) {
        // Copy the old file to the new
        BPTR src_lock = Lock(src_file, SHARED_LOCK);
        err = IoErr();
        D(if (src_lock == BNULL) bug("%s: Lock('%s', SHARED_LOCK): %ld\n", __func__, src_file, IoErr()));
        if (src_lock != BNULL) {
            ok = wbCopyLockCurrent(DOSBase, dst_file, src_lock);
            err = IoErr();
            D(if (!ok) bug("%s: Top level %s|%s copy to %s - (%ld)\n", __func__, sCURRDIR(), src_file, dst_file, (IPTR)err));
        }
    }

    SetIoErr(err);
    FreeVec(dst_file);

    return ok;
}

// Copy into this directory, respecting icons
BOOL _wbCopyIntoCurrentAt(struct Library *DOSBase, struct Library *IconBase, BPTR src_dir, CONST_STRPTR src_file, LONG targetX, LONG targetY)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    BOOL ok = FALSE;

    BPTR pwd = CurrentDir(src_dir);
    BPTR src_lock = Lock(src_file, SHARED_LOCK);
    LONG err = IoErr();
    CurrentDir(pwd);

    if (src_lock != BNULL) {
        // Copy the icon.
        BPTR pwd = CurrentDir(src_dir);
        struct DiskObject *diskobject = GetDiskObject(src_file);
        CurrentDir(pwd);
        if (diskobject != NULL) {
            // Set positioning information.
            diskobject->do_CurrentX = targetX;
            diskobject->do_CurrentY = targetY;
            // Write out to the new location.
            ok = PutDiskObject(src_file, diskobject);
            err = IoErr();
            FreeDiskObject(diskobject);
        }
        if (ok) {
            ok = wbCopyLockCurrent(DOSBase, src_file, src_lock);
            err = IoErr();
            if (!ok && diskobject != NULL) {
                DeleteDiskObject((STRPTR)src_file);
            }
        } else {
            UnLock(src_lock);
        }
    }

    SetIoErr(err);

    return ok;
}

// Move (rename) into this directory, respecting icons.
BOOL _wbMoveIntoCurrentAt(struct Library *DOSBase, struct Library *IconBase, BPTR src_dir, CONST_STRPTR src_file, LONG targetX, LONG targetY)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    BOOL ok = FALSE;

    BPTR pwd = CurrentDir(src_dir);
    char *abspath = wbAbspathCurrent(src_file);
    LONG err = IoErr();
    CurrentDir(pwd);
    if (abspath != NULL) {
        ok = Rename(abspath, src_file);
        err = IoErr();
        if (ok) {
            // Move the icon, also. We could just do a Rename() of it, but it's more 'Workbench safe'
            // to do a GetDiskObject()/PutDiskObject()/DeleteDiskObject()
            BPTR pwd = CurrentDir(src_dir);
            struct DiskObject *diskobject = GetDiskObject(src_file);
            CurrentDir(pwd);
            if (diskobject != NULL) {
                // Set positioning information.
                diskobject->do_CurrentX = targetX;
                diskobject->do_CurrentY = targetY;
                // Write out to the new location.
                ok = PutDiskObject(src_file, diskobject);
                err = IoErr();
                FreeDiskObject(diskobject);
                if (ok) {
                    // Remove the old disk object.
                    BPTR pwd = CurrentDir(src_dir);
                    ok = DeleteDiskObject((STRPTR)src_file);
                    err = IoErr();
                    CurrentDir(pwd);
                }
            }
        }
        FreeVec(abspath);
    }

    SetIoErr(err);

    return ok;
}
