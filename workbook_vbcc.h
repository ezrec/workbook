/*
 * VBCC Compatability Header
*/

#ifndef WORKBOOK_VBCC_H
#define WORKBOOK_VBCC_H

#include <proto/exec.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>
#include <clib/debug_protos.h>

#include <exec/alerts.h>
#include <exec/execbase.h>

#define PATH_MAX 1024
#define BNULL   ((BPTR)0)
#define AROS_BSTR_ADDR(s) (((STRPTR)BADDR(s))+1)
#define AROS_BSTR_strlen(s) (AROS_BSTR_ADDR(s)[-1])

#define SYS_DupStream   1
typedef CONST APTR RAWARG;
typedef unsigned long IPTR;
typedef long SIPTR;
#define STACKED
#define NEWLIST(listp) NewList((struct List *)listp)

typedef void (*VOID_FUNC)(void);
#define RAWFMTFUNC_STRING (VOID_FUNC)0 // Output to string given in PutChData
#define RAWFMTFUNC_SERIAL (VOID_FUNC)1 // Output to debug log (usually serial port)
#define RAWFMTFUNC_COUNT  (VOID_FUNC)2 // Just count characters, PutChData is a pointer to the counter (ULONG *)

static inline VOID bug(CONST_STRPTR format, ...) {
    CONST_STRPTR *args = &format;
    RawDoFmt(format, args + 1, RAWFMTFUNC_SERIAL, NULL);
}

#if DEBUG
#define D(x) x
#define ASSERT(x) do { \
    if (!(x)) { bug("%s: assertion (%s) == FALSE\n", __func__, #x); Alert(AN_Workbench); } \
} while (0)
#define ASSERT_VALID_PTR(x) do { ASSERT((IPTR)(x) > 1024); ASSERT(TypeOfMem((APTR)(x)) != 0); } while (0)
#define ASSERT_VALID_PROCESS(p) do { \
    ASSERT_VALID_PTR(p); ASSERT(((struct Node *)(p))->ln_Type == NT_PROCESS); \
} while (0)
#else
#define ASSERT(x) do { if (x) { } } while (0)
#define ASSERT_VALID_PTR(x) do { if (0) { (void)(x); } } while (0)
#define ASSERT_VALID_PROCESS(x) do { if (0) { (void)(x); } } while (0)
#define D(x)
#endif


BOOL __RegisterWorkbench(__reg("a6") void *, __reg("a0") struct MsgPort *port)="\tjsr\t-138(a6)";
#define RegisterWorkbench(port) __RegisterWorkbench(WorkbenchBase, (port))
BOOL __UnregisterWorkbench(__reg("a6") void *, __reg("a0") struct MsgPort *port)="\tjsr\t-144(a6)";
#define UnregisterWorkbench(port) __UnregisterWorkbench(WorkbenchBase, (port))
struct DiskObject * __GetNextAppIcon(__reg("a6") void *, __reg("a0") struct DiskObject *lastdiskobj, __reg("a1") char *text)="\tjsr\t-162(a6)";
#define GetNextAppIcon(lastdiskobj, text) __GetNextAppIcon(WorkbenchBase, lastdiskobj, text)

#define ForeachNode(list, node)                        \
for                                                    \
(                                                      \
    *(void **)&node = (void *)(((struct List *)(list))->lh_Head); \
    ((struct Node *)(node))->ln_Succ;                  \
    *(void **)&node = (void *)(((struct Node *)(node))->ln_Succ)  \
)

#define ForeachNodeSafe(list, current, next)              \
for                                                       \
( \
    *(void **)&current = (void *)(((struct List *)(list))->lh_Head); \
    ((*(void **)&next = (void *)((struct Node *)(current))->ln_Succ)) != NULL; \
    *(void **)&current = (void *)next \
)

static inline struct Node *REMOVE(struct Node *n)
{
    n->ln_Pred->ln_Succ = n->ln_Succ;
    n->ln_Succ->ln_Pred = n->ln_Pred;
    return n;
}


static inline struct Node *REMHEAD(struct List *l)
{
    if (l == NULL) return NULL;
    return l->lh_Head->ln_Succ ? REMOVE(l->lh_Head) : NULL;
}

static inline struct Node *GetHead(struct List *l) {
    if (l == NULL) return NULL;
    return l->lh_Head->ln_Succ ? l->lh_Head : NULL;
}

static inline ULONG __inline_UpdateAttrs(Object *o, struct GadgetInfo *gi, ULONG flags, Tag attr1, ...)
    {
        return DoMethod(o, OM_UPDATE, &attr1, gi, flags);
    }
#define UpdateAttrs(o, gi, flags, ...) __inline_UpdateAttrs(o, gi, flags, __VA_ARGS__)

static inline APTR __inline_NewObject(void *IntuitionBase, struct IClass * classPtr, CONST_STRPTR classID, Tag tag1, ...) {
    return NewObjectA(classPtr, classID, (struct TagItem *)&tag1);
}
#define NewObject(classPtr, classID, ...) __inline_NewObject(IntuitionBase, classPtr, classID, __VA_ARGS__)

static inline LONG STRLEN(CONST_STRPTR s)
{
    CONST_STRPTR _s = s;
    while (*_s++ != '\0');
    return _s - s - 1;
}

static inline STRPTR StrDup(CONST_STRPTR str) {
     STRPTR dup;
    ULONG  len;

    if (str == NULL) return NULL;

    len = STRLEN(str);
    dup = AllocVec(len + 1, MEMF_PUBLIC);
    if (dup != NULL) CopyMem(str, dup, len + 1);

    return dup;
}

static inline int stricmp(CONST_STRPTR a, CONST_STRPTR b) {
    for (; *a && *b; a++, b++) {
        char la = *a | 0x20;
        char lb = *b | 0x20;
        if ((la >= 'a' && la <= 'z') &&
            (lb >= 'a' && lb <= 'z')) {
            if (la != lb) {
                return lb - la;
            }
        } else {
            break;
        }
    }

    return *b - *a;
}

enum WBHM_Type
{
    WBHM_TYPE_SHOW,   /* Open all windows */
    WBHM_TYPE_HIDE,   /* Close all windows */
    WBHM_TYPE_OPEN,   /* Open a drawer */
    WBHM_TYPE_UPDATE  /* Update an object */
};

struct WBHandlerMessage
{
    struct Message    wbhm_Message;
    enum   WBHM_Type  wbhm_Type;       /* Type of message (see above) */

    union
    {
        struct
        {
            CONST_STRPTR      Name;    /* Name of drawer */
        } Open;

        struct
        {
            CONST_STRPTR      Name;    /* Name of object */
            LONG              Type;    /* Type of object (WBDRAWER, WBPROJECT, ...) */
        } Update;
    } wbhm_Data;
};

#define WBHM_SIZE (sizeof(struct WBHandlerMessage))
#define WBHM(msg) ((struct WBHandlerMessage *) (msg))

#define A0 a0
#define A1 a1
#define A2 a2
#define D0 d0
#define A6 a6
#define __REG(x) #x
#define AROS_UFHA(type, var, reg) __reg(__REG(reg)) type var
#define AROS_UFH2(type, func, arg1, arg2) type func(arg1, arg2)
#define AROS_UFH3(type, func, arg1, arg2, arg3) type func(arg1, arg2, arg3)
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT
#define AROS_PROCP(n) AROS_UFH2(SIPTR, n, AROS_UFHA(STRPTR, _argptr, A0), AROS_UFHA(ULONG,  _argsize, D0))
#define AROS_PROCH(n, _argptr, _argsize, _SysBase) \
    AROS_UFH2(SIPTR, n,                           \
        AROS_UFHA(STRPTR, _argptr, A0),           \
        AROS_UFHA(ULONG,  _argsize, D0)) { \
        __reg("a6") struct ExecBase *SysBase = *(struct ExecBase **)0x4;
#define AROS_PROCFUNC_INIT
#define AROS_PROCFUNC_EXIT }

struct FullJumpVec
{
    unsigned short jmp;
    void *vec;
};

struct phony_segment
{
    ULONG Size; /* Length of segment in # of bytes */
    BPTR  Next; /* Next segment (always 0 for this) */
};

#endif // WORKBOOK_VBCC_H
