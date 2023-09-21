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

#include <stddef.h>
#include <string.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/icon.h>
#include <proto/intuition.h>

#include <libraries/gadtools.h>

#include "wbcurrent.h"
#include "wbinfo.h"
#include "wbdoimage.h"

struct wbInfo {
    struct Library *wb_DOSBase;
    struct Library *wb_GadToolsBase;
    struct Library *wb_IntuitionBase;
    struct Library *wb_IconBase;
    struct Library *wb_LocaleBase;
    Class *wb_WBDoImage;

    // Resources
    struct Screen *Screen;

    // Information about the object
    CONST_STRPTR File;
    struct DiskObject *DiskObject;
    Object *IconImage;

    // Cached items
    LONG id_NumBlocks;
    LONG id_NumBlocksUsed;
    LONG id_BytesPerBlock;
    LONG fib_Size;
    LONG fib_NumBlocks;

    // Updatable items
    LONG do_StackSize;
    STRPTR do_DefaultTool;
    STRPTR do_ToolWindow;
    STRPTR *do_ToolTypes;
    BOOL DiskObjectModified;

    STRPTR fib_Comment;
    BOOL CommentModified;

    LONG fib_Protection;
    BOOL ProtectionModified;

    struct List *ToolTypes_List;
    struct Gadget *ToolTypes_ListView;
};

static void wbInfo_Main(struct wbInfo *wb, CONST_STRPTR file);

// NOTE: The 'ln_Name' is the file to open, assuming pr_CurrentDir is valid.
AROS_PROCH(wbInfo, argstr, argsize, SysBase)
{
    AROS_PROCFUNC_INIT

    struct Process *proc = (struct Process *)FindTask(NULL);
    D(bug("%s: Process %lx Enter\n", __func__, (IPTR)proc));

    CONST_STRPTR file = proc->pr_Task.tc_Node.ln_Name;

    // It's small. Just allocate on the stack.
    struct wbInfo wb = { 0 };

    wb.wb_DOSBase = OpenLibrary("dos.library", 0);
    if (wb.wb_DOSBase == NULL) goto error;

    wb.wb_IntuitionBase = OpenLibrary("intuition.library",0);
    if (wb.wb_IntuitionBase == NULL) goto error;

    wb.wb_GadToolsBase = OpenLibrary("gadtools.library",0);
    if (wb.wb_GadToolsBase == NULL) goto error;

    // Version 44 or later for DrawIconStateA
    wb.wb_IconBase = OpenLibrary("icon.library",44);
    if (wb.wb_IconBase == NULL) goto error;

    wb.wb_LocaleBase = OpenLibrary("locale.library", 0);
    if (wb.wb_LocaleBase == NULL) goto error;

    wbInfo_Main(&wb, file);

error:
    if (wb.wb_LocaleBase)
        CloseLibrary(wb.wb_LocaleBase);

    if (wb.wb_IconBase)
        CloseLibrary(wb.wb_IconBase);

    if (wb.wb_GadToolsBase)
        CloseLibrary(wb.wb_GadToolsBase);

    if (wb.wb_IntuitionBase)
        CloseLibrary(wb.wb_GadToolsBase);

    if (wb.wb_DOSBase)
        CloseLibrary(wb.wb_DOSBase);

    D(bug("%s: Process %lx Exit\n", __func__, (IPTR)proc));

    return 0;

    AROS_PROCFUNC_EXIT
}

#define IntuitionBase wb->wb_IntuitionBase
#define DOSBase       wb->wb_DOSBase
#define GadToolsBase  wb->wb_GadToolsBase
#define IconBase      wb->wb_IconBase
#define LocaleBase    wb->wb_LocaleBase

#define WBDoImage     wb->wb_WBDoImage

////////////////////// Helper functions

static STRPTR wbInfo_DupStr(CONST_STRPTR pstr)
{
    if (pstr == NULL) {
        pstr = "";
    }

    size_t len = strlen(pstr) + 1;
    STRPTR buff = AllocVec(len + 1, MEMF_ANY);
    CopyMem(pstr, buff, len);

    return buff;
}

static STRPTR *wbInfo_DupStrStr(STRPTR *pstr)
{
    STRPTR zero[] = {NULL};
    if (pstr == NULL) {
        pstr = &zero[0];
    }

    // Count the number of items in the array, and the total number of bytes in all ASCIIZ strings.
    size_t count;
    size_t bytes = 0;
    for (count = 0; pstr[count] != NULL; count++) {
        bytes += STRLEN(pstr[count]) + 1;
    }

    // Allocate a vector for the whole thing.
    STRPTR *pnew = AllocVec((count + 1) * sizeof(CONST_STRPTR) + bytes, MEMF_ANY);
    if (pnew) {
        STRPTR pbuf = (STRPTR)(&pnew[count+1]);
        size_t i;
        for (i = 0; i < count; i++) {
            pnew[i] = pbuf;
            for (CONST_STRPTR cp = pstr[i]; *cp != 0; *(pbuf++) = *(cp++));
            // End the ASCIIZ string.
            *(pbuf++) = 0;
        }
        // Add a NULL at the end of the array.
        pnew[i] = NULL;
    }

    return pnew;
}



// Return the absolute path of CurrentDir()
// Caller must FreeVec() the result.
static STRPTR wbInfo_CurrentPath(struct wbInfo *wb)
{
    STRPTR buff;
    STRPTR path = NULL;

    buff = AllocVec(PATH_MAX, MEMF_ANY);
    if (buff) {
        BPTR pwd = CurrentDir(BNULL);
        CurrentDir(pwd);
        if (NameFromLock(pwd, buff, PATH_MAX - 1)) {
            path = wbInfo_DupStr(buff);
        }
        FreeVec(buff);
    }

    return path;
}

enum {
    WBINFO_GADID_TITLE = 1,
    WBINFO_GADID_BLOCK_TOTAL,
    WBINFO_GADID_BLOCK_USED,
    WBINFO_GADID_BLOCK_FREE,
    WBINFO_GADID_BLOCK_SIZE,
    WBINFO_GADID_BYTE_SIZE,
    WBINFO_GADID_STACK_SIZE,
    WBINFO_GADID_VOLUME_STATE,
    WBINFO_GADID_ICON,
    WBINFO_GADID_DATESTAMP,
    WBINFO_GADID_PROT_SCRIPT,
    WBINFO_GADID_PROT_ARCHIVED,
    WBINFO_GADID_PROT_READABLE,
    WBINFO_GADID_PROT_WRITABLE,
    WBINFO_GADID_PROT_EXECUTABLE,
    WBINFO_GADID_PROT_DELETABLE,
    WBINFO_GADID_COMMENT,
    WBINFO_GADID_DEFAULT_TOOL,
    WBINFO_GADID_SAVE,
    WBINFO_GADID_CANCEL,
    WBINFO_GADID_TOOL_TYPES,
    WBINFO_GADID_TOOL_TYPES_NEW,
    WBINFO_GADID_TOOL_TYPES_DEL,
    WBINFO_GADID_TOOL_TYPES_INPUT,
};

#define WBINFO_FIELD_SIZE       35
#define WBINFO_WINDOW_WIDTH    400
#define WBINFO_TEXT_X            8
#define WBINFO_TEXT_Y           10
#define WBINFO_TEXT_INPUT_Y     (WBINFO_TEXT_Y + 6)
#define WBINFO_ICON_HEIGHT      64
#define WBINFO_ICON_WIDTH       96

#define WBINFO_IS_VOLUME(wb) (((wb)->DiskObject->do_Type) == WBDISK || ((wb)->DiskObject->do_Type) == WBDEVICE || ((wb)->DiskObject->do_Type) == WBKICK)

static struct Gadget *wbInfoGadgets_title(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    UBYTE do_Type = wb->DiskObject->do_Type;

    const CONST_STRPTR type[] = {
        "(Unknown)",
        "(Volume)",
        "(Drawer)",
        "(Tool)",
        "(Project)",
        "(Trashcan)",
    };
    if (do_Type >= sizeof(type)/sizeof(type[0])) {
        do_Type = 0;
    }
    if (WBINFO_IS_VOLUME(wb)) {
        do_Type = WBDISK;
    }

    CONST_STRPTR filename = wb->File;

    ng->ng_LeftEdge += 11 * WBINFO_TEXT_X;
    ng->ng_GadgetID = WBINFO_GADID_TITLE;
    ng->ng_GadgetText = (APTR)type[do_Type];
    ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;
    ng->ng_Width = 0;
    ng->ng_Height = WBINFO_TEXT_Y;
    gctx = CreateGadget(TEXT_KIND, gctx, ng, GTTX_Text, filename, (IPTR)TAG_END);

    ng->ng_TopEdge += gctx->Height;

    return gctx;
}

// Area to the right of the icon - protection checkboxes
static struct Gadget *wbInfoGadgets_icon_right(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    WORD topedge = ng->ng_TopEdge;

    if (WBINFO_IS_VOLUME(wb)) {
         // No protection fields.
        ng->ng_TopEdge += CHECKBOX_HEIGHT * 7;
        return gctx;
    }

    IPTR protection = wb->fib_Protection;
    protection ^= 0xf;

    CONST_STRPTR boxes[] = {
        "Script",
        "Archived",
        "Readable",
        "Writable",
        "Executable",
        "Deletable",
    };
    ULONG flags[] = {
        FIBF_SCRIPT,
        FIBF_ARCHIVE,
        FIBF_READ,
        FIBF_WRITE,
        FIBF_EXECUTE,
        FIBF_DELETE,
    };

    for (int i = 0; i < 6; i++) {
        // Place the checkbox.
        ng->ng_TopEdge = topedge + CHECKBOX_HEIGHT * i;
        ng->ng_LeftEdge = WBINFO_WINDOW_WIDTH - CHECKBOX_WIDTH;
        ng->ng_Width = CHECKBOX_HEIGHT; // Note the 'GTCB_Scaled=TRUE' later - we want a square.
        ng->ng_Height = CHECKBOX_HEIGHT;
        ng->ng_GadgetID = WBINFO_GADID_PROT_SCRIPT + i;
        ng->ng_GadgetText = (APTR)boxes[i];
        ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;

        BOOL checked = (protection & flags[i]) != 0;
        gctx = CreateGadget(CHECKBOX_KIND, gctx, ng, GTCB_Scaled, TRUE, GTCB_Checked, checked, TAG_END);
    }

    ng->ng_TopEdge += CHECKBOX_HEIGHT;

    return gctx;
}


static struct Gadget *wbInfoGadgets_icon_left(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    WORD leftedge = ng->ng_LeftEdge;

    struct entry {
        LONG id;
        CONST_STRPTR text;
        LONG value;
        BOOL isInput;
    };

    IPTR do_Type = wb->DiskObject->do_Type;

    IPTR id_blocks_total = wb->id_NumBlocks;
    IPTR id_blocks_used = wb->id_NumBlocksUsed;
    IPTR id_blocks_size = wb->id_BytesPerBlock;

    IPTR fib_blocks = wb->fib_NumBlocks;
    IPTR fib_bytes = wb->fib_Size;
    IPTR do_stacksize = wb->DiskObject->do_StackSize;

    const struct entry devicemap[] = {
        { WBINFO_GADID_BLOCK_TOTAL, "    Blocks:", (LONG)id_blocks_total, },
        { WBINFO_GADID_BLOCK_USED,  "      Used:", (LONG)id_blocks_used,},
        { WBINFO_GADID_BLOCK_FREE,  "      Free:", (LONG)(id_blocks_total - id_blocks_used),},
        { WBINFO_GADID_BLOCK_SIZE,  "Block size:", (LONG)id_blocks_size, },
        { 0 },
    };

    const struct entry emptymap[] = {
        { 0 },
    };

    const struct entry toolmap[] = {
        { WBINFO_GADID_BLOCK_SIZE, "    Blocks:", fib_blocks},
        { WBINFO_GADID_BYTE_SIZE,  "     Bytes:", fib_bytes},
        { WBINFO_GADID_STACK_SIZE, "     Stack:", do_stacksize, TRUE},
        { 0 },
    };

    const struct entry *map;

    switch (do_Type) {
    case WBDISK:
    case WBKICK:
    case WBDEVICE:
        map = devicemap;
        break;
    case WBGARBAGE:
    case WBDRAWER:
        map = emptymap;
        break;
    case WBTOOL:
    case WBPROJECT:
        map = toolmap;
        break;
    default:
        map = emptymap;
        break;
    }

    ng->ng_TopEdge += WBINFO_TEXT_Y / 2;

    for (int i = 0; map[i].id != 0; i++) {
        ng->ng_LeftEdge = leftedge + 11 * WBINFO_TEXT_X;
        ng->ng_Height = WBINFO_TEXT_Y;
        ng->ng_GadgetID = map[i].id;
        ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;
        ng->ng_GadgetText = (APTR)map[i].text;

        ng->ng_Width = 11 * WBINFO_TEXT_X;;
        if (map[i].isInput) {
            ng->ng_Height = WBINFO_TEXT_Y * 4 / 3;
            ng->ng_TopEdge += WBINFO_TEXT_Y / 2;
            gctx = CreateGadget(INTEGER_KIND, gctx, ng, GTIN_Number, (IPTR)map[i].value, (IPTR)GTNM_Justification, (IPTR)GTJ_LEFT, (IPTR)TAG_END);
            ng->ng_TopEdge += WBINFO_TEXT_Y / 2;
        } else {
            gctx = CreateGadget(NUMBER_KIND, gctx, ng, GTNM_Number, (IPTR)map[i].value, (IPTR)GTNM_Justification, (IPTR)GTJ_LEFT, (IPTR)TAG_END);
        }

        // Move down to next line
        ng->ng_TopEdge += WBINFO_TEXT_Y;
    }


    return gctx;
}

static struct Gadget *wbInfoGadgets_icon_image(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    IPTR width = WBINFO_ICON_WIDTH;
    IPTR height = WBINFO_ICON_HEIGHT;

    ng->ng_LeftEdge += WBINFO_TEXT_X * 11 + WBINFO_TEXT_X/2;
    ng->ng_Width = width;
    ng->ng_Height = height;
    ng->ng_GadgetText = NULL;

    if (wb->IconImage) {
        gctx = (struct Gadget *)NewObject(NULL, "buttongclass",
                GA_Width, width,
                GA_Height, height,
                GA_Top, ng->ng_TopEdge,
                GA_Left, ng->ng_LeftEdge,
                GA_Previous, gctx, (IPTR)GA_Image, wb->IconImage, (IPTR)TAG_END);
    } else {
        gctx = CreateGadget(BUTTON_KIND, gctx, ng, GA_Disabled, (IPTR)TRUE, (IPTR)GA_Text, "NO ICON", (IPTR)GTBB_Recessed, (IPTR)TRUE, (IPTR)TAG_END);
    }
    ng->ng_TopEdge += WBINFO_TEXT_Y * 6;

    return gctx;
}

static struct Gadget *wbInfoGadgets_datestamp(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    ng->ng_Height = WBINFO_TEXT_Y;
    ng->ng_GadgetID = WBINFO_GADID_DATESTAMP;
    ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;

    if (WBINFO_IS_VOLUME(wb)) {
        ng->ng_GadgetText = (APTR)"Created:";
    } else {
        ng->ng_GadgetText = (APTR)"Last Changed:";
    }

    ng->ng_Width = 20 * WBINFO_TEXT_X;
    gctx = CreateGadget(TEXT_KIND, gctx, ng, GTTX_Text, (IPTR)"11-May-2023 11:22:33", (IPTR)TAG_END);
    ng->ng_TopEdge += ng->ng_Height + WBINFO_TEXT_Y / 2;

    return gctx;
}

static struct Gadget *wbInfoGadgets_default_tool(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    ng->ng_GadgetID = WBINFO_GADID_DEFAULT_TOOL;
    ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;
    ng->ng_GadgetText = (APTR)"Default Tool:";

    ng->ng_Height = WBINFO_TEXT_INPUT_Y;
    ng->ng_Width = WBINFO_FIELD_SIZE * WBINFO_TEXT_X;
    gctx = CreateGadget(STRING_KIND, gctx, ng, GTST_String, (IPTR)wb->do_DefaultTool, GTST_MaxChars, PATH_MAX, (IPTR)TAG_END);
    ng->ng_TopEdge += ng->ng_Height + WBINFO_TEXT_Y / 2;

    return gctx;
}

static struct Gadget *wbInfoGadgets_comment(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    ng->ng_GadgetID = WBINFO_GADID_COMMENT;
    ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;
    ng->ng_GadgetText = (APTR)"Comment:";

    ng->ng_Height = WBINFO_TEXT_INPUT_Y;
    ng->ng_Width = WBINFO_FIELD_SIZE * WBINFO_TEXT_X;
    gctx = CreateGadget(STRING_KIND, gctx, ng, GTST_String, (IPTR)wb->fib_Comment, GTST_MaxChars, 80, (IPTR)TAG_END);
    ng->ng_TopEdge += ng->ng_Height + WBINFO_TEXT_Y / 2;

    return gctx;
}

static BOOL wbInfo_ToolTypesGet(struct wbInfo *wb)
{
    // Construct an exec list of the ToolTypes data
    STRPTR *pstr = wb->DiskObject->do_ToolTypes;
    size_t count;
    for (count = 0; pstr[count] != NULL; count++) {
        D(bug("%s: [%ld] %s\n", __func__, (IPTR)count, pstr[count]));
    }
    D(bug("%s: Got a ToolTypes that is %ld items long\n", __func__, (IPTR)count));
    struct List *tools = AllocVec(sizeof(struct List) + count * sizeof(struct Node), MEMF_ANY);
    if (tools != NULL) {
        NEWLIST(tools);
        struct Node *nodes = (struct Node *)&tools[1];
        for (size_t i = 0; i < count; i++) {
            nodes[i] = (struct Node){ .ln_Type = 0, .ln_Name = (STRPTR)pstr[i] };
            AddTail(tools, &nodes[i]);
        }

        // Mark as unmodified.
        tools->lh_Type = 0;
    }

    wb->ToolTypes_List = tools;

    return (tools != NULL);
}

static void wbInfo_ToolTypesPut(struct wbInfo *wb)
{
    struct List *tooltypes = wb->ToolTypes_List;

    if (tooltypes->lh_Type) {
        // Modified - make a STRSTR * to send.
        size_t count = 0;
        struct Node *node;
        ForeachNode(tooltypes, node) {
            count++;
        }
        STRPTR *pstr = AllocVec((count + 1) * sizeof(STRPTR), MEMF_ANY);
        if (pstr) {
            count = 0;
            ForeachNode(tooltypes, node) {
                pstr[count++] = node->ln_Name;
            }
            pstr[count] = NULL;

            // Update.
            FreeVec(wb->do_ToolTypes);
            wb->do_ToolTypes = wbInfo_DupStrStr(pstr);
            FreeVec(pstr);
        }
        // Free modified nodes.
        struct Node *tmp;
        ForeachNodeSafe(tooltypes, node, tmp) {
            if (node->ln_Type) {
                Remove(node);
                FreeVec(node);
            }
        }
    }

    // Free the list and all the (originally allocated) nodes in it.
    FreeVec(tooltypes);
    wb->ToolTypes_List = NULL;
}

static void wbInfo_ToolTypesDEBUG(struct wbInfo *wb, CONST_STRPTR func, LONG where, struct List *tooltypes)
{
    struct Node *curr;
    LONG index = 0;
    ForeachNode(tooltypes, curr) {
        D(bug("%s: [%ld] %s%s%s\n", func, (IPTR)index, curr->ln_Type ? "*" : " ", curr->ln_Name, (where == index) ? " <--": ""));
        D(index++);
    }
}

static void wbInfo_ToolTypesNew(struct wbInfo *wb, LONG entry, CONST_STRPTR str)
{
    struct List *tooltypes = wb->ToolTypes_List;
    struct Node *node;

    D(bug("Insert: %ld '%s'\n", (IPTR)entry, str));
    LONG index = entry;
    size_t len = STRLEN(str) + 1;
    node = AllocVec(sizeof(struct Node) + len, MEMF_ANY | MEMF_CLEAR);
    if (node != NULL) {
        tooltypes->lh_Type = 1; // Modified list.
        node->ln_Type = 1, // Modified node.
        node->ln_Name = (STRPTR)(&node[1]),
        CopyMem(str, node->ln_Name, len);
        if (index == 0) {
            D(bug("New: [%ld] at head: '%s' (%s)\n", (IPTR)entry, node->ln_Name, str));
            AddHead(tooltypes, node);
        } else if (index < 0) {
            D(bug("New: [%ld] at tail: '%s' (%s)\n", (IPTR)entry, node->ln_Name, str));
            AddTail(tooltypes, node);
        } else {
            struct Node *curr;
            ForeachNode(tooltypes, curr) {
                if (--index == 0) {
                    D(bug("New: [%ld] insert: '%s' (%s)\n", (IPTR)entry, node->ln_Name, str));
                    Insert(tooltypes, node, curr);
                    break;
                }
            }
            if (index != 0) {
                D(bug("New: [%ld] at tail: '%s' (%s)\n", (IPTR)entry, node->ln_Name, str));
                AddTail(tooltypes, node);
            }
        }
        tooltypes->lh_Type = 1;
    }

    wbInfo_ToolTypesDEBUG(wb, "New", entry, wb->ToolTypes_List);
}

static void wbInfo_ToolTypesDel(struct wbInfo *wb, LONG entry)
{
    struct List *tooltypes = wb->ToolTypes_List;
    LONG index = entry;
    struct Node *curr;
    ForeachNode(tooltypes, curr) {
        if (index-- == 0) {
            Remove(curr);
            if (curr->ln_Type) {
                FreeVec(curr);
            }
            tooltypes->lh_Type = 1;
            break;
        }
    }

    wbInfo_ToolTypesDEBUG(wb, "Del", entry, wb->ToolTypes_List);
}

static void wbInfo_ToolTypesReplace(struct wbInfo *wb, LONG index, CONST_STRPTR pstr)
{
    wbInfo_ToolTypesNew(wb, index, pstr);
    wbInfo_ToolTypesDel(wb, index+1);
}

static struct Gadget *wbInfoGadgets_tool_types(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    WORD leftedge = ng->ng_LeftEdge;
    const WORD list_height = 7 * WBINFO_TEXT_Y + WBINFO_TEXT_INPUT_Y;

    // Add editing control
    ng->ng_GadgetID = WBINFO_GADID_TOOL_TYPES_INPUT;
    ng->ng_GadgetText = (APTR)NULL;
    ng->ng_Height = WBINFO_TEXT_INPUT_Y;
    ng->ng_Width = WBINFO_FIELD_SIZE * WBINFO_TEXT_X;
    gctx = CreateGadget(STRING_KIND, gctx, ng, GA_Disabled, FALSE, GTST_MaxChars, 128, (IPTR)TAG_END);
    struct Gadget *input = gctx;

    // Tool types
    ng->ng_GadgetID = WBINFO_GADID_TOOL_TYPES;
    ng->ng_Flags = PLACETEXT_LEFT | NG_HIGHLABEL;
    ng->ng_GadgetText = (APTR)"Tool Types:";
    ng->ng_Height = list_height;
    gctx = CreateGadget(LISTVIEW_KIND, gctx, ng, GTLV_Labels, wb->ToolTypes_List, GTLV_ShowSelected, (IPTR)input, (IPTR)TAG_END);
    D(bug("%s: wb->ToolTypes_ListView=0x%lx\n", __func__, (IPTR)gctx));
    wb->ToolTypes_ListView = gctx;
    ng->ng_TopEdge += list_height;

    ng->ng_TopEdge -= WBINFO_TEXT_INPUT_Y;

    // Add 'Del' button
    WORD button_width = 5 * WBINFO_TEXT_X;
    ng->ng_Height = WBINFO_TEXT_INPUT_Y;
    ng->ng_Width = button_width;
    ng->ng_LeftEdge -= ng->ng_Width + WBINFO_TEXT_X/2;
    ng->ng_GadgetID = WBINFO_GADID_TOOL_TYPES_DEL;
    ng->ng_GadgetText = (APTR)"Del";
    gctx = CreateGadget(BUTTON_KIND, gctx, ng, (IPTR)TAG_END);

    // Add 'New' button
    ng->ng_LeftEdge -= ng->ng_Width + WBINFO_TEXT_X/2;
    ng->ng_GadgetID = WBINFO_GADID_TOOL_TYPES_NEW;
    ng->ng_GadgetText = (APTR)"New";
    gctx = CreateGadget(BUTTON_KIND, gctx, ng, (IPTR)TAG_END);

    ng->ng_LeftEdge = leftedge;
    ng->ng_TopEdge += ng->ng_Height + WBINFO_TEXT_INPUT_Y*4/3;

    return gctx;
}

static struct Gadget *wbInfoGadgets_save_cancel(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    ng->ng_Height = WBINFO_TEXT_INPUT_Y;
    ng->ng_Width = 6 * WBINFO_TEXT_X;
    ng->ng_GadgetText = (APTR)"Save";
    ng->ng_GadgetID = WBINFO_GADID_SAVE;
    gctx = CreateGadget(BUTTON_KIND, gctx, ng, (IPTR)TAG_END);

    ng->ng_Width = 8 * WBINFO_TEXT_X;
    ng->ng_LeftEdge = WBINFO_WINDOW_WIDTH - ng->ng_Width - WBINFO_TEXT_X;
    ng->ng_GadgetText = (APTR)"Cancel";
    ng->ng_GadgetID = WBINFO_GADID_CANCEL;
    gctx = CreateGadget(BUTTON_KIND, gctx, ng, (IPTR)TAG_END);

    ng->ng_TopEdge += ng->ng_Height + WBINFO_TEXT_Y/2;

    return gctx;
}

static struct Gadget *wbInfoGadgets(struct wbInfo *wb, struct Gadget *gctx, struct NewGadget *ng)
{
    WORD leftedge = ng->ng_LeftEdge;

    gctx = wbInfoGadgets_title(wb, gctx, ng);
    WORD titletop = ng->ng_TopEdge;
    gctx = wbInfoGadgets_icon_right(wb, gctx, ng);
    WORD proptop = ng->ng_TopEdge;

    ng->ng_TopEdge = titletop;
    ng->ng_LeftEdge = leftedge;
    gctx = wbInfoGadgets_icon_left(wb, gctx, ng);

    ng->ng_TopEdge = titletop;
    ng->ng_LeftEdge = leftedge + 11 * WBINFO_TEXT_X;
    gctx = wbInfoGadgets_icon_image(wb, gctx, ng);

    ng->ng_TopEdge = proptop;
    ng->ng_LeftEdge = leftedge + 14 * WBINFO_TEXT_X;
    gctx = wbInfoGadgets_datestamp(wb, gctx, ng);

    switch (wb->DiskObject->do_Type) {

    case WBDEVICE:
    case WBKICK:
    case WBDISK:
        gctx = wbInfoGadgets_default_tool(wb, gctx, ng);
        break;
    case WBDRAWER: // fallthrough
    case WBTOOL:
        gctx = wbInfoGadgets_comment(wb, gctx, ng);
        gctx = wbInfoGadgets_tool_types(wb, gctx, ng);
        break;
    case WBPROJECT:
        gctx = wbInfoGadgets_comment(wb, gctx, ng);
        gctx = wbInfoGadgets_default_tool(wb, gctx, ng);
        gctx = wbInfoGadgets_tool_types(wb, gctx, ng);
        break;
    case WBGARBAGE:
        break;
    }

    ng->ng_LeftEdge = leftedge;
    gctx = wbInfoGadgets_save_cancel(wb, gctx, ng);

    return gctx;
}

static BOOL wbInfo_DoMessage(struct wbInfo *wb, struct IntuiMessage *msg, BOOL *save_ptr)
{
    struct Window *win = msg->IDCMPWindow;
    struct Gadget *gad = (struct Gadget *)msg->IAddress;

    ULONG msgClass = msg->Class;
    UWORD msgCode = msg->Code;
    IPTR tmp = 0;
    BOOL save = FALSE;
    BOOL done = FALSE;

    D(bug("%s: msg %ld, %ld\n", __func__, (IPTR)msgClass, (IPTR)msgCode));
    switch (msgClass) {
    case IDCMP_REFRESHWINDOW:
        GT_BeginRefresh(win);
        GT_EndRefresh(win, TRUE);
        break;
    case IDCMP_CLOSEWINDOW:
        done = TRUE;
        break;
    case IDCMP_VANILLAKEY:
        switch (msgCode) {
        case '\r':
            // fallthrough
        case 'o':
            save = TRUE;
            done = TRUE;
            break;
        case 'c':
            save = FALSE;
            done = TRUE;
            break;
        }
        break;
    case IDCMP_GADGETUP:
        switch (gad->GadgetID) {
        case WBINFO_GADID_PROT_SCRIPT:
            D(bug("%s: WBINFO_GADID_PROT_SCRIPT modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTCB_Checked, &tmp, TAG_END);
            wb->fib_Protection &= ~FIBF_SCRIPT;
            if (tmp) {
                wb->fib_Protection |= FIBF_SCRIPT;
            }
            wb->ProtectionModified = TRUE;
            break;
        case WBINFO_GADID_PROT_ARCHIVED:
            D(bug("%s: WBINFO_GADID_PROT_ARCHIVED modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTCB_Checked, &tmp, TAG_END);
            wb->fib_Protection &= ~FIBF_ARCHIVE;
            if (tmp) {
                wb->fib_Protection |= FIBF_ARCHIVE;
            }
            wb->ProtectionModified = TRUE;
            break;
        case WBINFO_GADID_PROT_READABLE:
            D(bug("%s: WBINFO_GADID_PROT_READABLE modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTCB_Checked, &tmp, TAG_END);
            wb->fib_Protection &= ~FIBF_READ;
            if (!tmp) {
                wb->fib_Protection |= FIBF_READ;
            }
            wb->ProtectionModified = TRUE;
            break;
        case WBINFO_GADID_PROT_WRITABLE:
            D(bug("%s: WBINFO_GADID_PROT_WRITABLE modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTCB_Checked, &tmp, TAG_END);
            wb->fib_Protection &= ~FIBF_WRITE;
            if (!tmp) {
                wb->fib_Protection |= FIBF_WRITE;
            }
            wb->ProtectionModified = TRUE;
            break;
        case WBINFO_GADID_PROT_EXECUTABLE:
            D(bug("%s: WBINFO_GADID_PROT_EXECUTABLE modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTCB_Checked, &tmp, TAG_END);
            wb->fib_Protection &= ~FIBF_EXECUTE;
            if (!tmp) {
                wb->fib_Protection |= FIBF_EXECUTE;
            }
            wb->ProtectionModified = TRUE;
            break;
        case WBINFO_GADID_PROT_DELETABLE:
            D(bug("%s: WBINFO_GADID_PROT_DELETABLE modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTCB_Checked, &tmp, TAG_END);
            wb->fib_Protection &= ~FIBF_DELETE;
            if (!tmp) {
                wb->fib_Protection |= FIBF_DELETE;
            }
            wb->ProtectionModified = TRUE;
            break;
         case WBINFO_GADID_STACK_SIZE:
            D(bug("%s: WBINFO_GADID_STACK_SIZE modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, GTIN_Number, &tmp, TAG_END);
            wb->do_StackSize = tmp;
            wb->DiskObjectModified = TRUE;
            break;
        case WBINFO_GADID_COMMENT:
            D(bug("%s: WBINFO_GADID_COMMENT modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, (IPTR)GTST_String,(IPTR) &tmp, (IPTR)TAG_END);
            STRPTR cm_dup = wbInfo_DupStr((APTR)tmp);
            if (cm_dup) {
                FreeVec(wb->fib_Comment);
                wb->fib_Comment = cm_dup;
            }
            wb->CommentModified = TRUE;
            break;
        case WBINFO_GADID_DEFAULT_TOOL:
            D(bug("%s: WBINFO_GADID_DEFAULT_TOOL modified\n", __func__));
            GT_GetGadgetAttrs(gad, win, NULL, (IPTR)GTST_String, (IPTR)&tmp, (IPTR)TAG_END);
            STRPTR dt_dup = wbInfo_DupStr((APTR)tmp);
            if (dt_dup) {
                FreeVec(wb->do_DefaultTool);
                wb->do_DefaultTool = dt_dup;
            }
            wb->DiskObjectModified = TRUE;
            break;
        case WBINFO_GADID_TOOL_TYPES_NEW:
            D(bug("%s: WBINFO_GADID_TOOL_TYPES_NEW - Add new entry\n", __func__));
            if (wb->ToolTypes_ListView) {
                D(bug("%s: Gadget %lx (Selected: %ld)\n", __func__, (IPTR)wb->ToolTypes_ListView, (IPTR)tmp));
                GT_GetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, (IPTR)GTLV_Selected, &tmp, (IPTR)TAG_END);
                GT_SetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, (IPTR)GTLV_Labels, (IPTR)~0, (IPTR)TAG_END);
                wbInfo_ToolTypesNew(wb, tmp, "(new)");
                GT_SetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, (IPTR)GTLV_Labels, (IPTR)wb->ToolTypes_List, (IPTR)TAG_END);
                wb->DiskObjectModified = TRUE;
            }
            break;
        case WBINFO_GADID_TOOL_TYPES_DEL:
            D(bug("%s: WBINFO_GADID_TOOL_TYPES_DEL - Del this entry\n", __func__));
            if (wb->ToolTypes_ListView) {
                GT_GetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, (IPTR)GTLV_Selected, &tmp, (IPTR)TAG_END);
                D(bug("%s: Gadget %lx (Selected: %ld)\n", __func__, (IPTR)wb->ToolTypes_ListView, (IPTR)tmp));
                GT_SetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, (IPTR)GTLV_Labels, (IPTR)~0, (IPTR)TAG_END);
                wbInfo_ToolTypesDel(wb, tmp);
                GT_SetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, GTLV_Labels, (IPTR)wb->ToolTypes_List, TAG_END);
                wb->DiskObjectModified = TRUE;
            }
            break;
         case WBINFO_GADID_TOOL_TYPES_INPUT:
            D(bug("%s: WBINFO_GADID_TOOL_TYPES_INPUT - Replace current entry\n", __func__));
            if (wb->ToolTypes_ListView) {
                STRPTR replacement="";
                GT_GetGadgetAttrs(gad, win, NULL, GTST_String, &replacement, TAG_END);
                GT_GetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, GTLV_Selected, &tmp, TAG_END);
                D(bug("%s: Gadget %lx (Selected: %ld, Input: '%s')\n", __func__, (IPTR)wb->ToolTypes_ListView, (IPTR)tmp, replacement));
                // Duplicate, as the STRING_KIND field embedded in the LISTVIEW_KIND
                // vanishes as soon as the GTLV_Labels is set to ~0.
                replacement = wbInfo_DupStr(replacement);
                GT_SetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, GTLV_Labels, (IPTR)~0, TAG_END);
                wbInfo_ToolTypesReplace(wb, tmp, replacement);
                FreeVec(replacement);
                GT_SetGadgetAttrs(wb->ToolTypes_ListView, win, NULL, GTLV_Labels, (IPTR)wb->ToolTypes_List, TAG_END);
                wb->DiskObjectModified = TRUE;
            }
            break;
        case WBINFO_GADID_SAVE:
            save = TRUE;
            done = TRUE;
            break;
        case WBINFO_GADID_CANCEL:
            save = FALSE;
            done = TRUE;
            break;
        }
        break;
    }

    *save_ptr |= save;

    return done;
}

// GUI window runner.
static BOOL wbInfo_Window(struct wbInfo *wb)
{
    BOOL save = FALSE;
    wb->CommentModified = FALSE;
    wb->ProtectionModified = FALSE;
    wb->DiskObjectModified = FALSE;

    D(bug("%s: Enter\n", __func__));
    BOOL ok = wbInfo_ToolTypesGet(wb);
    D(if (!ok) bug("%s wbInfo_ToolTypesGet failed\n", __func__));
    if (ok) {
        STRPTR path = wbInfo_CurrentPath(wb);
        D(if (!path) bug("%s wbInfo_CurrentPath failed\n", __func__));
        if (path) {
            D(bug("%s: path='%s', file='%s'\n", __func__, path, wb->File));
            struct VisualInfo *vi = GetVisualInfo(wb->Screen, TAG_END);
            D(if (!vi) bug("%s: vi is NULL\n", __func__));
            if (vi) {
                D(bug("%s: Got vi\n", __func__));
                struct NewGadget ng = {
                    .ng_VisualInfo = vi,
                    .ng_TopEdge = WBINFO_TEXT_Y/2,
                    .ng_LeftEdge = WBINFO_TEXT_X/2,
                };
                struct Gadget *glist = NULL;
                struct Gadget *gctx = CreateContext(&glist);
                D(if (!gctx) bug("%s: gctx is NULL\n", __func__));
                if (gctx) {
                    wbInfoGadgets(wb, gctx, &ng);
                    struct Window *win = OpenWindowTags(NULL,
                                    WA_Top, 20,
                                    WA_Left, 20,
                                    WA_Width, 16 + WBINFO_WINDOW_WIDTH,
                                    WA_Height, 16 + ng.ng_TopEdge,
                                    WA_Title, path,
                                    WA_Gadgets, glist,
                                    WA_PubScreen, wb->Screen,
                                    WA_AutoAdjust, FALSE,
                                    WA_SimpleRefresh, TRUE,
                                    WA_DepthGadget, TRUE,
                                    WA_DragBar, TRUE,
                                    WA_Activate, TRUE,
                                    WA_CloseGadget, TRUE,
                                    WA_GimmeZeroZero, TRUE,
                                    WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | CHECKBOXIDCMP | NUMBERIDCMP | BUTTONIDCMP | IDCMP_VANILLAKEY,
                                    TAG_END);
                    D(if (!win) bug("%s: win is NULL\n", __func__));
                    if (win) {
                        D(bug("%s: Got win\n", __func__));
                        GT_RefreshWindow(win, NULL);
                        BOOL done = FALSE;
                        while (!done) {
                            D(bug("%s: sleep...\n", __func__));
                            ULONG signals = 1L << win->UserPort->mp_SigBit;
                            if (Wait(signals) & signals) {
                                struct IntuiMessage *msg;

                                while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
                                    struct IntuiMessage msgCopy = *msg;
                                    GT_ReplyIMsg(msg);

                                    done |= wbInfo_DoMessage(wb, &msgCopy, &save);
                                }
                            }
                        }
                        D(bug("%s: save=%s\n", __func__, save ? "TRUE" : "FALSE"));
                        CloseWindow(win);
                    }
                    D(bug("%s: free gadgets\n", __func__));
                    FreeGadgets(glist);
                }
                D(bug("%s: free visual\n", __func__));
                FreeVisualInfo(vi);
            }
            FreeVec(path);
        }

        wbInfo_ToolTypesPut(wb);
    }

    D(bug("%s: Exit save=%s\n", __func__, save ? "TRUE" : "FALSE"));

    return save;
}

// Main wrapper - does all the file I/O
static void wbInfo_Main(struct wbInfo *wb, CONST_STRPTR file)
{
    D(bug("%s: '%s' Enter\n", __func__, file));

    wb->wb_WBDoImage = WBDoImage_MakeClass(IntuitionBase);
    if (wb->wb_WBDoImage == NULL) {
        return;
    }

    wb->Screen = LockPubScreen(NULL);
    if (!wb->Screen) {
        D(bug("%s: Screen lock failed\n", __func__));
        FreeClass(wb->wb_WBDoImage);
        return;
    }

    wb->File = file;
    BPTR lock = Lock(wb->File, SHARED_LOCK);
    D(if (lock == BNULL) bug("%s: '%s' lock failed\n", __func__, wb->File));
    if (lock) {
        struct FileInfoBlock *fib = AllocDosObjectTags(DOS_FIB, TAG_END);
        if (fib) {
            if (Examine(lock, fib)) {
                wb->fib_Protection = fib->fib_Protection;
                wb->fib_Comment = wbInfo_DupStr(fib->fib_Comment);
                wb->fib_Size = fib->fib_Size;
                wb->fib_NumBlocks = fib->fib_NumBlocks;
            } else {
                wb->fib_Comment = wbInfo_DupStr(NULL);
            }
            FreeDosObject(DOS_FIB, fib);
        } else {
            wb->fib_Comment = wbInfo_DupStr(NULL);
        }

        // Must be BADDR aligned!
        UWORD *_idbuff = AllocVec(sizeof(WORD) * (sizeof(struct InfoData)/sizeof(WORD) + 1), MEMF_ANY);
        if (_idbuff) {
            // Optional! Some filesystems may not support it.
            struct InfoData *id = BADDR(MKBADDR(&_idbuff[1]));
            if (Info(lock, id)) {
                wb->id_NumBlocks = id->id_NumBlocks;
                wb->id_NumBlocksUsed = id->id_NumBlocksUsed;
                wb->id_BytesPerBlock = id->id_BytesPerBlock;
            }
            FreeVec(_idbuff);
        }

        UnLock(lock);
    }

    BOOL save = FALSE;
    wb->DiskObject = GetDiskObjectNew(wb->File);
    D(if (!wb->DiskObject) bug("%s: GetDiskObjectNew('%s') failed\n", __func__, wb->File));
    if (wb->DiskObject) {
        wb->IconImage = NewObject(WBDoImage, NULL, IA_Screen, wb->Screen, IA_Data, wb->DiskObject, 
                IA_Width, WBINFO_ICON_WIDTH,
                IA_Height, WBINFO_ICON_HEIGHT,
                TAG_END);
        D(if (!wb->IconImage) bug("%s: No imagery in the icon?\n", __func__));

        // Duplicate the diskobject fields we may modify.
        wb->do_StackSize = wb->DiskObject->do_StackSize;
        wb->do_DefaultTool = wbInfo_DupStr(wb->DiskObject->do_DefaultTool);
        if (wb->do_DefaultTool) {
            wb->do_ToolWindow = wbInfo_DupStr(wb->DiskObject->do_ToolWindow);
            if (wb->do_ToolWindow) {
                wb->do_ToolTypes = wbInfo_DupStrStr(wb->DiskObject->do_ToolTypes);
                if (wb->do_ToolTypes) {

                    // Instantiate and run the window.
                    save = wbInfo_Window(wb);
                    D(bug("%s: save=%s\n", __func__, save ? "TRUE" : "FALSE"));
                    D(bug("%s: CommentModified=%s\n", __func__, wb->CommentModified ? "TRUE" : "FALSE"));
                    D(bug("%s: ProtectionModified=%s\n", __func__, wb->ProtectionModified ? "TRUE" : "FALSE"));
                    D(bug("%s: DiskObjectModified=%s\n", __func__, wb->DiskObjectModified ? "TRUE" : "FALSE"));

                    if (save && wb->DiskObjectModified) {
                        struct DiskObject *do_local = GetDiskObjectNew(wb->File);
                        D(if (!do_local) bug("%s: GetDiskObjectNew('%s') failed for modification\n", __func__, do_local));
                        D(if (do_local) bug("%s: '%s.info' saved\n", __func__, wb->File));
                        if (do_local) {
                            struct DiskObject do_tmp = *do_local;
                            do_tmp.do_StackSize = wb->do_StackSize;
                            do_tmp.do_DefaultTool = wb->do_DefaultTool;
                            for (int i = 0; wb->do_ToolTypes[i] != NULL; i++) {
                                D(bug("%s: TT [%ld] '%s'\n", __func__, (IPTR)i, wb->do_ToolTypes[i]));
                            }
                            do_tmp.do_ToolTypes = wb->do_ToolTypes;
                            do_tmp.do_ToolWindow = wb->do_ToolWindow;
                            D(BOOL ok = )PutIconTags(wb->File, &do_tmp, TAG_END);
                            D(if (!ok) bug("%s: PutDiskObject('%s') failed\n", __func__, wb->File));
                            D(if (ok) bug("%s: '%s.info' saved\n", __func__, wb->File));
                            FreeDiskObject(do_local);
                        }
                    }
                    FreeVec(wb->do_ToolTypes);
                }
                FreeVec(wb->do_ToolWindow);
            }
            FreeVec(wb->do_DefaultTool);
        }
        if (wb->IconImage) {
            DisposeObject(wb->IconImage);
        }
        FreeDiskObject(wb->DiskObject);
    }

    if (save && wb->ProtectionModified) {
        SetProtection(wb->File, wb->fib_Protection);
    }

    if (save && wb->CommentModified) {
        SetComment(wb->File, wb->fib_Comment);
    }
    FreeVec(wb->fib_Comment);

    UnlockPubScreen(NULL, wb->Screen);

    FreeClass(wb->wb_WBDoImage);

    D(bug("%s: '%s' Exit\n", __func__, file));
}
