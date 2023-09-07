/*
 * Copyright (C) 2012, The AROS Development Team
 * All right reserved.
 * Author: Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1
 */

#include <proto/dos.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>

#include <libraries/gadtools.h>
#include <intuition/sghooks.h>

#include "workbook_intern.h"

struct Region *wbClipWindow(struct WorkbookBase *wb, struct Window *win)
{
    struct Region *clip;

    /* Install new clip region */
    if ((clip = NewRegion())) {
        struct Rectangle rect = {
                .MinX = win->BorderLeft,
                .MinY = win->BorderTop,
                .MaxX = win->Width - win->BorderRight - 1,
                .MaxY = win->Height - win->BorderBottom - 1,
        };
        if (!OrRectRegion(clip, &rect)) {
                DisposeRegion(clip);
                clip = NULL;
        }
    }

    /* Install new clip region */
    return InstallClipRegion(win->WLayer, clip);
}

void wbUnclipWindow(struct WorkbookBase *wb, struct Window *win, struct Region *clip)
{
    clip = InstallClipRegion(win->WLayer, clip);
    if (clip) {
            DisposeRegion(clip);
    }
}

static ULONG _wbPopupActionHook(struct Hook *hook, struct SGWork *sgw, ULONG *msg)
{
    CONST_STRPTR forbidden = (CONST_STRPTR)hook->h_Data;
    ULONG rc = ~(ULONG)0;

    D(bug("msg: 0x%lx, op: 0x%lx, code: %lc\n", *msg, sgw->EditOp, sgw->Code));
    switch (*msg) {
    case SGH_KEY:
        if ((sgw->EditOp == EO_REPLACECHAR) || (sgw->EditOp == EO_INSERTCHAR)) {
            BOOL ok = TRUE;
            for (; ok && forbidden != NULL && *forbidden != 0; forbidden++) {
                if (*forbidden == sgw->Code) {
                    D(bug("Forbidden: '%lc'\n", sgw->Code));
                    ok = FALSE;
                }
            }

            if (ok) {
                sgw->WorkBuffer[sgw->BufferPos - 1] = sgw->Code;
            } else {
                sgw->Actions |= SGA_BEEP;
                sgw->Actions &= ~SGA_USE;
            }
        }
        break;
    default:
        rc = 0;
        break;
    }

    return rc;
}

IPTR wbPopupAction(struct WorkbookBase *wb,
                         CONST_STRPTR title,
                         CONST_STRPTR description,
                         CONST_STRPTR request,
                         STRPTR saveBuffer, // Can be NULL,
                         LONG saveBufferSize, // Can be 0
                         CONST_STRPTR forbidden,
                         wbPopupActionFunc action, APTR arg)
{
    IPTR rc = 0;
    struct Window *win = NULL;
    struct Gadget *glist = NULL, *gctx, *inputGadget=NULL;
    enum {
           descriptionField,
           requestField,
           inputField,
           okButton,
           cancelButton
    };
    struct NewGadget newGadget = {0};
    CONST_STRPTR inputBuffer;

    const WORD winWidth = 400;
    const WORD winHeight = 100;

    struct Screen *screen = NULL;
    void *vi = NULL;

    screen = LockPubScreen(NULL);
    if (screen == NULL) {
            goto exit;
    }

    vi = GetVisualInfo(screen, TAG_END);
    if (vi == NULL) {
            goto exit;
    }

    gctx = CreateContext(&glist);
    if (gctx == NULL) {
            goto exit;
    }

    WORD top_border = screen->WBorTop + (screen->Font->ta_YSize + 1);

    // Description
    newGadget.ng_TopEdge = top_border + 8;
    newGadget.ng_Width = STRLEN(descriptionField) * 8;
    newGadget.ng_LeftEdge = 20;
    newGadget.ng_VisualInfo = vi;
    newGadget.ng_Height = 12;
    newGadget.ng_GadgetText = (UBYTE *)""; // Initialize with empty string
    newGadget.ng_GadgetID = descriptionField;
    newGadget.ng_Flags = NG_HIGHLABEL;
    gctx = CreateGadget(TEXT_KIND, gctx, &newGadget, GTTX_Text, description, TAG_END);

    // Request
    newGadget.ng_TopEdge += 20;
    newGadget.ng_Width = 10 * 8;
    newGadget.ng_Height = 12;
    newGadget.ng_GadgetText = (UBYTE *)""; // Initialize with empty string
    newGadget.ng_GadgetID = requestField;
    newGadget.ng_Flags = NG_HIGHLABEL;
    gctx = CreateGadget(TEXT_KIND, gctx, &newGadget, GTTX_Text, request, TAG_END);

    struct Hook input_hook = {
        .h_Entry = HookEntry,
        .h_SubEntry = _wbPopupActionHook,
        .h_Data = (void *)forbidden,
    };

    // Create an input field for the command
    newGadget.ng_LeftEdge += newGadget.ng_Width;
    newGadget.ng_Width = winWidth - newGadget.ng_LeftEdge - 20;
    newGadget.ng_Height = 18;
    newGadget.ng_GadgetText = (UBYTE *)""; // Initialize with empty string
    newGadget.ng_GadgetID = inputField;
    inputGadget = gctx = CreateGadget(STRING_KIND, gctx, &newGadget,
                   GTST_String, saveBuffer,
                   GTST_MaxChars, saveBufferSize > 0 ? saveBufferSize : 80,
                   GACT_RELVERIFY, TRUE,
                   GTST_EditHook, forbidden ? &input_hook : NULL,
                   TAG_END);

    if (gctx == NULL) {
        D(bug("No input field!"));
        goto exit;
    }
    inputBuffer = ((struct StringInfo *)(gctx->SpecialInfo))->Buffer;

    // Create the "Execute" button
    newGadget.ng_LeftEdge = 20;
    newGadget.ng_TopEdge += 24;
    newGadget.ng_Width = 60;
    newGadget.ng_Height = 20;
    newGadget.ng_GadgetText = "_Ok";
    newGadget.ng_GadgetID = okButton;
    gctx = CreateGadget(BUTTON_KIND, gctx, &newGadget, GT_Underscore, '_', TAG_END);

    if (gctx == NULL) {
        D(bug("No Ok button!"));
        goto exit;
    }

    // Create the "Cancel" button
    newGadget.ng_Width = 60;
    newGadget.ng_LeftEdge = winWidth - 20 - newGadget.ng_Width;
    newGadget.ng_GadgetText = "_Cancel";
    newGadget.ng_GadgetID = cancelButton;
    gctx = CreateGadget(BUTTON_KIND, gctx, &newGadget, GT_Underscore, '_', TAG_END);

    if (gctx == NULL) {
        D(bug("No Cancel button!"));
        goto exit;
    }

    // Create a simple window for the dialog
    win = OpenWindowTags(NULL,
        WA_Left, 20,
        WA_Top, 20,
        WA_Width, winWidth,
        WA_Height, winHeight,
        WA_Title, title,
        WA_Gadgets, glist,
        WA_PubScreen, screen,
        WA_AutoAdjust, TRUE,
        WA_SimpleRefresh, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_Activate, TRUE,
        WA_CloseGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP | IDCMP_VANILLAKEY,
        TAG_END);

    if (win == NULL) {
        goto exit;
    }

    GT_RefreshWindow(win, NULL);

    // Activate text entry.
    ActivateGadget(inputGadget, win, NULL);

    // Event loop
    BOOL done = FALSE;
    BOOL do_action = FALSE;
    ULONG signals = 1L << win->UserPort->mp_SigBit;

    while (!done) {
        ULONG result = Wait(signals);

        if (result & signals) {
            struct IntuiMessage *msg;

            while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
                struct Gadget *gad = (struct Gadget *)msg->IAddress;

                ULONG msgClass = msg->Class;
                UWORD msgCode = msg->Code;
                GT_ReplyIMsg(msg);

                switch (msgClass) {
                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(win);
                    GT_EndRefresh(win, TRUE);
                    break;
                case IDCMP_CLOSEWINDOW:
                    done = TRUE;
                    break;
                case IDCMP_VANILLAKEY:
                    D(bug("msgCode: %ld\n", msgCode));
                    switch (msgCode) {
                    case '\r':
                        // fallthrough
                    case 'o':
                        do_action = TRUE;
                        done = TRUE;
                        break;
                    case 'c':
                        done = TRUE;
                        break;
                    }
                    break;
                case IDCMP_GADGETUP:
                    switch (gad->GadgetID) {
                    case inputField:
                        // fallthrough
                    case okButton:
                        // "Ok" button was pressed
                        do_action = TRUE;
                        done = TRUE;
                        break;
                    case cancelButton:
                        // "Cancel" button was pressed
                        done = TRUE;
                        break;
                    }
                    break;
                }
            }
        }
    }

    if (do_action) {
        // If we're going to do it, hide this window first.
        HideWindow(win);
        rc = action(wb, inputBuffer, arg);
    }

    // Save the input for next time, regardless.
    if (saveBuffer != NULL && saveBufferSize > 0) {
        CopyMem(inputBuffer, saveBuffer, saveBufferSize);
        saveBuffer[saveBufferSize-1] = 0;
    }

exit:
    // Cleanup and close
    FreeGadgets(glist);
    CloseWindow(win);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, screen);

    return rc;
}

VOID wbPopupIoErr(struct WorkbookBase *wb, CONST_STRPTR title, LONG ioerr, CONST_STRPTR prefix)
{
    struct EasyStruct es = {
       .es_StructSize = sizeof(es),
       .es_Flags = 0,
       .es_Title = (STRPTR)title,
       .es_TextFormat = "%s",
       .es_GadgetFormat = "Ok",
    };
    char buff[256];
    if (ioerr == 0) {
        strcpy(buff, prefix);
        strcat(buff, ": ????");
    } else {
        Fault(ioerr, prefix, buff, sizeof(buff));
    }
    EasyRequest(0, &es, 0, buff);
}

