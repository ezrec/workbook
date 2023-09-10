// Unit tests for Workbook

#define TEST(category, testcase) for (CONST_STRPTR TESTCASE=#category "." #testcase; TESTCASE != NULL; TESTCASE=NULL)

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
