// Unit tests for Workbook

#ifndef WORKBOOK_TEST_H
#define TEST(category, testcase) for (CONST_STRPTR TESTCASE=#category "." #testcase; PASSED && TESTCASE != NULL; TESTCASE=NULL)

#define EXPECT_EQ(a, b) do { if ((a) != (b)) { bug("%s: Expected %s == %s\n", TESTCASE, #a, #b); PASSED=FALSE; } } while (0)
#define EXPECT_NE(a, b) do { if ((a) == (b)) { bug("%s: Expected %s != %s\n", TESTCASE, #a, #b); PASSED=FALSE; } } while (0)
#define EXPECT_STRING(a, b) do { CONST_STRPTR a_ = (a), b_ = (b); if (strcmp(a, b) != 0) { bug("%s: Expected %s == %s\n", TESTCASE, a_, b_); PASSED=FALSE; } } while (0)
#define EXPECT_LOCK(lock, string) do { \
    BPTR l_ = Lock(string, SHARED_LOCK); \
    if (l_ == BNULL) { \
        bug("%s: ERROR - %s is not lockable - %ld\n", TESTCASE, string, IoErr()); \
    } else { \
        if (SameLock(lock, l_) != LOCK_SAME) { \
            bug("%s: Expected %lx == %s\n", TESTCASE, (IPTR)lock, string); \
        } \
        UnLock(l_); \
   } \
} while (0)
#define EXPECT_FALSE(a) EXPECT_EQ(a, FALSE)
#define EXPECT_TRUE(a) EXPECT_NE(a, FALSE)

struct TestFS {
    CONST_STRPTR Path;
    CONST_STRPTR Contents;
};

static BOOL  _TEST_FS(struct WorkbookBase *wb, const struct TestFS *fsarr) {
    BOOL ok = TRUE;
    const struct TestFS *fs = fsarr;
    for (; fs != NULL && fs->Path != NULL; fs++) {
        if (fs->Contents == NULL) {
            BPTR lock = CreateDir(fs->Path);
            if (lock == BNULL) {
                bug("%s: Unable to create: %ld\n", fs->Path, IoErr());
                // No need to delete this - never existed.
                fs--;
                ok = FALSE;
                break;
            }
            UnLock(lock);
        } else {
            BPTR fh = Open(fs->Path, MODE_NEWFILE);
            if (fh == BNULL) {
                bug("%s: Unable to open: %ld\n", fs->Path, IoErr());
                ok = FALSE;
                break;
            }
            LONG rc = FPuts(fh, fs->Contents);
            LONG err = IoErr();
            Close(fh);
            if (rc < 0) {
                bug("%s: Unable to write: %ld\n", fs->Path, err);
                ok = FALSE;
                break;
            }
        }
    }

    if (!ok) {
        for (; fs >= fsarr; fs--) {
            bug("%s: Cleaning up (from error)\n", fs->Path);
            DeleteFile(fs->Path);
        }
    }

    return ok;
}

static void _UNTEST_FS(struct WorkbookBase *wb, const struct TestFS *fsarr) {
    const struct TestFS *fs = fsarr;
    for (; fs != NULL && fs->Path != NULL; fs++);

    if (fs != NULL && fs->Path == NULL) {
        for (fs--; fs >= fsarr; fs--) {
            bug("%s: Cleaning up\n", fs->Path);
            DeleteFile(fs->Path);
        }
    }
}

#define TEST_FS(fileset) EXPECT_TRUE(_TEST_FS(wb, fileset))
#define UNTEST_FS(fileset) _UNTEST_FS(wb, fileset)

#define WORKBOOK_TEST_H  DEBUG
#elif(WORKBOOK_TEST_H)

TEST(wbCopyBumpCurrent, copydir) {
    TEST_MEMAVAIL();

    // Test case tests
    BPTR lock = Lock("TESTCASE:Application", SHARED_LOCK);
    if (lock) {
        BPTR old = CurrentDir(lock);
        BOOL ok = wbCopyBumpCurrent("sc");
        LONG err = IoErr();
        bug("%s: 'sc' -> 'Copy_of_sc': %s (%ld)\n", TESTCASE, ok ? "TRUE" : "FALSE", (IPTR)err);
        CurrentDir(old);
        UnLock(lock);
    } else {
        bug("%s: TESTCASE:Application could not be locked\n", TESTCASE);
    }

    TEST_MEMUSED();
}

TEST(wbDeleteFromCurrent, existing)
{
    TEST_MEMAVAIL();

    BPTR lock = Lock("TESTCASE:Application", SHARED_LOCK);
    if (lock) {
        BPTR old = CurrentDir(lock);
        BOOL ok = wbDeleteFromCurrent("Copy_of_sc", FALSE);
        LONG err = IoErr();
        bug("%s: 'Copy_of_sc': %s (%ld)\n", TESTCASE, ok ? "TRUE" : "FALSE", (IPTR)err);
        CurrentDir(old);
        UnLock(lock);
    } else {
        bug("%s: TESTCASE:Application could not be locked\n", TESTCASE);
    }

    TEST_MEMUSED();
}

#endif
