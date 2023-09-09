/*
    Copyright (C) 2011, The AROS Development Team. All rights reserved.

    Desc: Workbook classes
*/
#ifndef WORKBOOK_CLASSES_H
#define WORKBOOK_CLASSES_H

#include <utility/tagitem.h>
#include <clib/alib_protos.h>

#include "workbook_intern.h"

/* WBApp class
 *
 * Workbench replacement
 */

/* Attributes */
#define WBAA_Dummy               (TAG_USER | 0x40400000)
#define WBAA_Screen              (WBAA_Dummy+0)         // (struct Screen *)

/* Methods */
#define WBAM_Dummy               (TAG_USER | 0x40400100)
#define WBAM_Workbench           (WBAM_Dummy+0)
#define WBAM_ForSelected         (WBAM_Dummy+1)         // Coerce message for all selected items, in Task context.
#define WBAM_ClearSelected       (WBAM_Dummy+2)         // Clear all selections.

struct wbam_ForSelected {
    STACKED ULONG             MethodID;
    STACKED Msg               wbamf_Msg;   // Message to send to all selected icons
};

Class *WBApp_MakeClass(struct WorkbookBase *wb);

#define WBApp        wb->wb_WBApp

/* WBWindow class
 * This is a 'scrolled view' of a directory of
 * icons, that also creates and manages its
 * own struct Window.
 *
 * Use a WBWA_Path of NULL to generate the background
 * window of AppIcons.
 *
 * NOTE: The caller must have already added the DOS
 *       devices to the Workbench AppIcon list!
 */

/* Attributes (also takes all WA_* tags) */
#define WBWA_Dummy               (TAG_USER | 0x40410000)
#define WBWA_Path                (WBWA_Dummy+0)  // (CONST_STRPTR) [OM_NEW, OM_GET]
#define WBWA_UserPort            (WBWA_Dummy+1)  // (struct MsgPort *) [OM_NEW]
#define WBWA_Window              (WBWA_Dummy+2)  // (struct Window *) [OM_GET]
#define WBWA_Screen              (WBWA_Dummy+3)  // (struct Screen *) [OM_NEW]

/* Methods */
#define WBWM_Dummy               (TAG_USER | 0x40410100)
#define WBWM_NewSize             (WBWM_Dummy+0)  /* N/A */
#define WBWM_MenuPick            (WBWM_Dummy+1)  /* struct wbwm_MenuPick {} */
#define WBWM_IntuiTick           (WBWM_Dummy+2)  /* N/A */
#define WBWM_Hide                (WBWM_Dummy+3)  /* N/A */
#define WBWM_Show                (WBWM_Dummy+4)  /* N/A */
#define WBWM_Refresh             (WBWM_Dummy+5)  /* N/A */
#define WBWM_ForSelected         (WBWM_Dummy+6)  /* Msg */

struct wbwm_MenuPick {
    STACKED ULONG             MethodID;
    STACKED struct MenuItem  *wbwmp_MenuItem;
    STACKED UWORD             wbwmp_MenuNumber;
};

struct wbwm_ForSelected {
    STACKED ULONG             MethodID;
    STACKED Msg               wbwmf_Msg;    // Msg to send to all selected icons in the window.
};

Class *WBWindow_MakeClass(struct WorkbookBase *wb);

#define WBWindow        wb->wb_WBWindow

/* WBVirtual class
 *
 * This class handles drawing and clipping a
 * child object this is a subclass of 'gadgetclass'
 */

/* Attributes */
#define WBVA_Dummy               (TAG_USER | 0x40420000)
#define WBVA_Gadget              (WBVA_Dummy+0)  /* Object * */
#define WBVA_VirtLeft            (WBVA_Dummy+1)  /* WORD */
#define WBVA_VirtTop             (WBVA_Dummy+2)  /* WORD */
#define WBVA_VirtWidth           (WBVA_Dummy+3)  /* WORD */
#define WBVA_VirtHeight          (WBVA_Dummy+4)  /* WORD */

/* Methods */
#define WBVM_Dummy               (TAG_USER | 0x40420100)

Class *WBVirtual_MakeClass(struct WorkbookBase *wb);

#define WBVirtual        wb->wb_WBVirtual

/* WBSet class
 *
 * A set of gadgets, packed together.
 *
 * Treat this as a smarter 'groupclass' object.
 */

/* Attributes */
#define WBSA_Dummy               (TAG_USER | 0x40430000)
#define WBSA_MemberCount         (WBSA_Dummy + 2)   // ULONG Count of the number of members
#define WBSA_SelectedCount       (WBSA_Dummy + 3)   // ULONG Count of the number of selected members

/* Methods */
#define WBSM_Dummy               (TAG_USER | 0x40430100)
#define WBSM_Select              (WBSM_Dummy + 0)
#define WBSM_Clean_Up            (WBSM_Dummy + 1)

struct wbsm_Select {
    STACKED ULONG MethodID;
    STACKED struct GadgetInfo	*wbss_GInfo;	/* gadget context		*/
    STACKED ULONG wbss_All;   // True to select all, false to select none.
};

struct wbsm_CleanUp {
    STACKED ULONG MethodID;
    STACKED struct GadgetInfo	*wbscu_GInfo;	/* gadget context		*/
};


Class *WBSet_MakeClass(struct WorkbookBase *wb);

#define WBSet        wb->wb_WBSet


/* WBIcon class
 *
 * This class represents a single icon, and takes
 * care of all of its drawing.
 *
 * You can create from a WBIA_File, or a 
 * WBIA_Icon. Do not use both!
 */

/* Attributes */
#define WBIA_Dummy               (TAG_USER | 0x40440000)
#define WBIA_File                (WBIA_Dummy+0)        // (CONST_STRPTR) 'VOLNAME:' or FilePart()
#define WBIA_Label               (WBIA_Dummy+1)        // (CONST_STRPTR) Visible label, overriding WBIA_File [optional]
#define WBIA_Label               (WBIA_Dummy+3)        /* CONST_STRPTR */
#define WBIA_Screen              (WBIA_Dummy+4)        /* struct Screen * */

/* Methods */
#define WBIM_Dummy               (TAG_USER | 0x40440100)
#define WBIM_Open                (WBIM_Dummy + 0)        // NA
#define WBIM_Copy                (WBIM_Dummy + 1)        // NA
#define WBIM_Rename              (WBIM_Dummy + 2)        // NA
#define WBIM_Info                (WBIM_Dummy + 3)        // NA
#define WBIM_Snapshot            (WBIM_Dummy + 4)        // NA
#define WBIM_Unsnapshot          (WBIM_Dummy + 5)        // NA
#define WBIM_Leave_Out           (WBIM_Dummy + 6)        // NA
#define WBIM_Put_Away            (WBIM_Dummy + 7)        // NA
#define WBIM_Delete              (WBIM_Dummy + 8)        // NA
#define WBIM_Format              (WBIM_Dummy + 9)        // NA
#define WBIM_Empty_Trash         (WBIM_Dummy + 10)       // NA

/* Return flags for all WBIM_ methods */
#define WBIF_OK                 (0)         // Window imagery unchanged.
#define WBIF_REFRESH            (1 << 0)    // Refresh of containing window requested

Class *WBIcon_MakeClass(struct WorkbookBase *wb);

#define WBIcon        wb->wb_WBIcon

#endif /* WORKBOOK_CLASSES_H */
