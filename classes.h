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
#define WBAM_ReportSelected      (WBAM_Dummy+3)         // Get a report of selected items, using WBOPENA_* tags
#define WBAM_DragDropBegin       (WBAM_Dummy+4)         // Enter drag/drop mode.
#define WBAM_DragDropUpdate      (WBAM_Dummy+5)         // Update
#define WBAM_DragDropEnd         (WBAM_Dummy+6)         // Leave drag/drop mode.
#define WBAM_InvalidateContents  (WBAM_Dummy+7)         // (BPTR) Invalidate contents for all windows.

struct wbam_ForSelected {
    STACKED ULONG             MethodID;
    STACKED Msg               wbamf_Msg;   // Message to send to all selected icons
};

struct wbam_ReportSelected {
    STACKED ULONG   MethodID;
    STACKED struct TagItem **wbamr_ReportTags;  // Referenced pointer will set to a list of WBOPENA_* tags.
                                                // Set this to a pointer to (struct TagItem *) NULL to start.
                                                // NOTE: This memory must be freed by FreeTagItems()
                                                //       *BEFORE* any changes to the icon selection states!
                                                // The list will contain WBOPENA_ArgLock and WBOPENA_ArgName sets.
};

struct wbam_InvalidateContents {
    STACKED ULONG MethodID;
    STACKED BPTR  wbami_VolumeLock;
};

Class *WBApp_MakeClass(struct WorkbookBase *wb);

#define WBApp        wb->wb_WBApp

/* WBWindow class
 * This is a 'scrolled view' of a directory of
 * icons, that also creates and manages its
 * own struct Window.
 *
 * Use a WBWA_Lock of BNULL to generate the background
 * window of AppIcons.
 *
 * NOTE: The caller must have already added the DOS
 *       devices to the Workbench AppIcon list!
 */

/* Attributes (also takes all WA_* tags) */
#define WBWA_Dummy               (TAG_USER | 0x40410000)
#define WBWA_Lock                (WBWA_Dummy+0)  // (BPTR) [OM_NEW, OM_GET] The class will DupLock() if needed.
#define WBWA_UserPort            (WBWA_Dummy+1)  // (struct MsgPort *) [OM_NEW]
#define WBWA_Window              (WBWA_Dummy+2)  // (struct Window *) [OM_GET]
#define WBWA_Screen              (WBWA_Dummy+3)  // (struct Screen *) [OM_NEW]
#define WBWA_NotifyPort          (WBWA_Dummy+4)  // (struct MsgPort *) [OM_NEW]

/* Methods */
#define WBWM_Dummy               (TAG_USER | 0x40410100)
#define WBWM_NewSize             (WBWM_Dummy+0)  /* N/A */
#define WBWM_MenuPick            (WBWM_Dummy+1)  /* struct wbwm_MenuPick {} */
#define WBWM_IntuiTick           (WBWM_Dummy+2)  /* N/A */
#define WBWM_Hide                (WBWM_Dummy+3)  /* N/A */
#define WBWM_Show                (WBWM_Dummy+4)  /* N/A */
#define WBWM_Refresh             (WBWM_Dummy+5)  /* N/A */
#define WBWM_ForSelected         (WBWM_Dummy+6)  /* Msg */
#define WBWM_InvalidateContents  (WBWM_Dummy+7)  // (BPTR) invalidate if BPTR is NULL, or SameLock() as parent.
#define WBWM_CacheContents       (WBWM_Dummy+8)  /* N/A */
#define WBWM_ReportSelected      (WBWM_Dummy+9)  /* struct wbwm_ReportSelected */
#define WBWM_Front               (WBWM_Dummy+10) // N/A

struct wbwm_MenuPick {
    STACKED ULONG             MethodID;
    STACKED struct MenuItem  *wbwmp_MenuItem;
    STACKED UWORD             wbwmp_MenuNumber;
};

struct wbwm_ForSelected {
    STACKED ULONG             MethodID;
    STACKED Msg               wbwmf_Msg;    // Msg to send to all selected icons in the window.
};

struct wbwm_ReportSelected {
    STACKED ULONG             MethodID;
    STACKED struct TagItem  **wbwmr_ReportTags; // Must be freed with FreeTagItems() after success
};

struct wbwm_InvalidateContents {
    STACKED ULONG             MethodID;
    STACKED BPTR wbwmi_VolumeLock;
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
#define WBSA_ViewModes          (WBSA_Dummy + 0) // (UWORD) A 'DrawerData->dd_ViewModes' value
#define WBSA_Backdrop           (WBSA_Dummy + 1) // (BOOL) Is this the Backdrop set?

/* Methods */
#define WBSM_Dummy               (TAG_USER | 0x40430100)
#define WBSM_Select              (WBSM_Dummy + 0)
#define WBSM_Clean_Up            (WBSM_Dummy + 1)
#define WBSM_Arrange             (WBSM_Dummy + 2)

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
 * This class represents a single icon, and takes care of all of its drawing.
 *
 * You can create from:
 *
 * Filesystem:
 *    WBIA_File         (CONST_STRPTR) 'VOLUME:' name.
 *    WBIA_Label        (CONST_STRPTR) label to display
 *    WBIA_ParentLock   (BPTR) BNULL
 * Drawer/Tool/Project:
 *    WBIA_File         (CONST_STRPTR) FilePart() of filename
 *    WBIA_Label        (CONST_STRPTR) label to display [optional]
 *    WBIA_ParentLock   (BPTR) lock to containing drawer/volume
 */

/* Attributes */
#define WBIA_Dummy               (TAG_USER | 0x40440000)
#define WBIA_File                (WBIA_Dummy+0)        // (CONST_STRPTR) [OM_NEW] 'VOLNAME:' or FilePart()
#define WBIA_Label               (WBIA_Dummy+1)        // (CONST_STRPTR) [OM_NEW] Visible label, overriding WBIA_File [optional]
#define WBIA_ParentLock          (WBIA_Dummy+2)        // (BPTR) [OM_NEW] to containing drawer/volume
#define WBIA_Screen              (WBIA_Dummy+3)        // (struct Screen) [OM_NEW] to reference for layout and drawing.
#define WBIA_ListView            (WBIA_Dummy+4)        // (BOOL) [OM_NEW, OM_SET] List, not icon, rendering.
#define WBIA_ListLabelWidth      (WBIA_Dummy+5)        // (ULONG) [OM_NEW, OM_SET] Label width, in characters.
#define WBIA_HitBox              (WBIA_Dummy+6)        // (struct Rectangle) [OM_GET] Icon hit box
#define WBIA_FibProtection       (WBIA_Dummy+16)       // (ULONG) [OM_GET] FileInfoBlock->fib_Protection of the file.
#define WBIA_FibSize             (WBIA_Dummy+17)       // (ULONG) [OM_GET] FileInfoBlock->fib_Size of the file.
#define WBIA_FibDateStamp        (WBIA_Dummy+19)       // (struct DateStamp *) [OM_GET] FileInfoBlock->fib_DateStamp of the file.
#define WBIA_DoType              (WBIA_Dummy+32)       // (UBYTE) [OM_GET] DiskObject->do_Type
#define WBIA_DoCurrentX          (WBIA_Dummy+35)       // (LONG) [OM_SET,OM_GET] DiskObject->do_CurrentX
#define WBIA_DoCurrentY          (WBIA_Dummy+36)       // (LONG) [OM_SET,OM_GET] DiskObject->do_CurrentY
#define WBIA_Backdrop            (WBIA_Dummy+37)       // (BOOL) [OM_SET,OM_GET] Is this a backdrop icon?

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
#define WBIM_DragDropAdd         (WBIM_Dummy + 11)       // (GadgetInfo *, Object *WBDragDrop) use WBDM_Add to add icon imagery
#define WBIM_MoveBy              (WBIM_Dummy + 12)       // (GadgetInfo *, LONG deltaX, LONG deltaY)

/* Return flags for all WBIM_ methods */
#define WBIF_OK                 (0)        // Window imagery unchanged.
#define WBIF_REFRESH           (1 << 0)    // Refresh of imagery only.
#define WBIF_UPDATE            (1 << 1)    // Update of containing window contents.

struct wbim_DragDropAdd {
    STACKED ULONG MethodID;
    STACKED struct GadgetInfo *wbimd_GInfo;
    STACKED Object *wbimd_DragDrop;
};

struct wbim_MoveBy {
    STACKED ULONG MethodID;
    STACKED struct GadgetInfo *wbimm_GInfo;
    STACKED LONG   wbimm_DeltaX;
    STACKED LONG   wbimm_DeltaY;
};

Class *WBIcon_MakeClass(struct WorkbookBase *wb);

#define WBIcon        wb->wb_WBIcon

/* WBDragDrop class
 *
 * A DiskObject drag & drop manager.
 *
 * The drag n drop manager takes a set of DiskObjects, and renders overlay imagery to move them around.
 * When WBMD_End is called, the imagery is erased.
 */

/* Attributes */
#define WBDA_Dummy              (TAG_USER | 0x40450000)
#define WBDA_Screen             (WBDM_Dummy + 0)    // (struct Screen *) [OM_NEW]
#define WBDA_OriginX            (WBDM_Dummy + 1)    // (UWORD) [OM_GET] Screen's MouseX at start of drag/drop
#define WBDA_OriginY            (WBDM_Dummy + 2)    // (UWORD) [OM_GET] Screen's MouseY at start of drag/drop

/* Methods */
//
// OM_NEW, OM_DISPOSE (once DnD has begun)
#define WBDM_Dummy              (TAG_USER | 0x40450100)
#define WBDM_Begin              (WBDM_Dummy + 0)    // Begin DnD dragging.
#define WBDM_Update             (WBDM_Dummy + 1)    // Update DnD region.
#define WBDM_End                (WBDM_Dummy + 2)    // End DnD dragging.
#define WBDM_Clear              (WBDM_Dummy + 3)    // Clear DnD list.
#define WBDM_Add                (WBDM_Dummy + 4)    // Add imagery to DnD list.

struct wbdm_Add {
    STACKED ULONG MethodID;
    STACKED ULONG wbdma_ImageType;       // One of WBDT_xxxx
    STACKED APTR  wbdma_ImageData;       // Image data
};

#define WBDT_IMAGE              0 // wbdm_Image is (struct *Image) (xor) imagery
#define WBDT_RECTANGLE          1 // wbdm_Image is (struct *Rectangle) (xor) imagery

Class *WBDragDrop_MakeClass(struct WorkbookBase *wb);

#define WBDragDrop  wb->wb_WBDragDrop

// Generic methods
#define WBxM_Dummy               (TAG_USER | 0x40460100)
#define WBxM_DragDropped         (WBxM_Dummy+0)  /* (struct wbwm_DragDropped) */

struct wbxm_DragDropped {
    STACKED ULONG             MethodID;
    STACKED struct GadgetInfo *wbxmd_GInfo;
    STACKED LONG              wbxmd_MouseX;
    STACKED LONG              wbxmd_MouseY;
    STACKED LONG              wbxmd_OriginX;
    STACKED LONG              wbxmd_OriginY;
};

/* WBBackdrop Class
 *
 * A .backdrop manager.
 *
 * The .backdrop manager manages the .backdrop for a volume.
 */

/* Attributes */
#define WBBA_Dummy              (TAG_USER | 0x40450000)

/* Methods */
//
// OM_NEW, OM_DISPOSE
#define WBBM_Dummy              (TAG_USER | 0x40450100)
#define WBBM_LockIs             (WBBM_Dummy + 0)    // (BPTR) Is lock a backdrop for any managed volume?
#define WBBM_LockNext           (WBBM_Dummy + 1)    // (BPTR) -> (BPTR) Get the next lock (start with BNULL)
#define WBBM_LockAdd            (WBBM_Dummy + 2)    // (BPTR) Add file for a lock to the backdrop for its volume.
#define WBBM_LockDel            (WBBM_Dummy + 3)    // (BPTR) Del file for a lock from the backdrop for its volume.
#define WBBM_VolumeAdd          (WBBM_Dummy + 4)    // (BPTR) Manage .backdrop entries for a volume.
#define WBBM_VolumeDel          (WBBM_Dummy + 5)    // (BPTR) Stop managing .backdrop entries for a volume.

struct wbbm_Lock {
    STACKED ULONG MethodID;
    STACKED BPTR  wbbml_Lock;
};

Class *WBBackdrop_MakeClass(struct WorkbookBase *wb);

#define WBBackdrop  wb->wb_WBBackdrop


#endif /* WORKBOOK_CLASSES_H */
