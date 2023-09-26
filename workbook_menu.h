/*
 * Copyright (C) 2011, The AROS Development Team.  All rights reserved.
 *
 * Licensed under the AROS PUBLIC LICENSE (APL) Version 1.1
 */

#ifndef WORKBOOK_MENU_H
#define WORKBOOK_MENU_H

#define Broken NM_ITEMDISABLED |

/* Handy macros */
#define WBMENU_ID_(id, name, cmd, flags, mutex)      ((IPTR)(id))
#define WBMENU_TITLE_(id, name, cmd, flags, mutex)   { NM_TITLE, name, cmd, flags, mutex, (APTR)id }
#define WBMENU_ITEM_(id, name, cmd, flags, mutex)    { NM_ITEM, name, cmd, flags, mutex, (APTR)id }
#define WBMENU_SUBITEM_(id, name, cmd, flags, mutex) { NM_SUB, name, cmd, flags, mutex, (APTR)id }

#define WBMENU_ID(x)                     WBMENU_ID_(x)
#define WBMENU_TITLE(x)                  WBMENU_TITLE_(x)
#define WBMENU_ITEM(x)                   WBMENU_ITEM_(x)
#define WBMENU_BAR                       { NM_ITEM, NM_BARLABEL, 0, 0, 0, NULL }
#define WBMENU_SUBTITLE(x)               WBMENU_ITEM_(x)
#define WBMENU_SUBITEM(x)                WBMENU_SUBITEM_(x)
#define WBMENU_SUBBAR                    { NM_SUB, NM_BARLABEL, 0, 0, 0, NULL }

#define WBMENU_ITEM_ID(item) ((IPTR)GTMENUITEM_USERDATA(item))

/* Workbench Menu */
#define WBMENU_WB               0, AS_STRING(WB_NAME), 0, 0, 0
#define WBMENU_WB_BACKDROP      1, "Backdrop",  "B", MENUTOGGLE | CHECKIT | CHECKED, 0
#define WBMENU_WB_EXECUTE       2, "Execute",   "E", 0, 0
#define WBMENU_WB_SHELL         3, "Shell",     "W", 0, 0
#define WBMENU_WB_ABOUT         4, "About...",    0, 0, 0
#define WBMENU_WB_QUIT          5, "Quit",      "Q", 0, 0
#define WBMENU_WB_SHUTDOWN      6, "Shutdown",    0, 0, 0
#define WBMENU_WB_CUST_UPDATER  11, "Updater",    0, 0, 0
#define WBMENU_WB_CUST_AMISTORE 12, "Amistore",   0, 0, 0

/* Window Menu */
#define WBMENU_WN               20, "Window", 0, 0, 0
#define WBMENU_WN_NEW_DRAWER        21, "New drawer",      "N", 0, 0
#define WBMENU_WN_OPEN_PARENT       22, "Open parent",     "K", 0, 0
#define WBMENU_WN_UPDATE            23, "Update",            0, 0, 0
#define WBMENU_WN_SELECT_CONTENTS   24, "Select contents", "A", 0, 0
#define WBMENU_WN_CLEAN_UP          25, "Clean Up",        ".", 0, 0
#define WBMENU_WN__SNAP         40, "Snapshot", 0, 0, 0
#define WBMENU_WN__SNAP_WINDOW      41, "Window",            0, 0, 0
#define WBMENU_WN__SNAP_ALL         42, "All",               0, 0, 0
#define WBMENU_WN__SHOW         45, "Show", 0, 0, 0
#define WBMENU_WN__SHOW_ICONS       46, "Only icons",      "-", MENUTOGGLE|CHECKIT|CHECKED, ~((1 << 0))
#define WBMENU_WN__SHOW_ALL         47, "All files",       "+", MENUTOGGLE|CHECKIT, ~((1 << 1))
#define WBMENU_WN__VIEW         50, "View by", 0, 0, 0
#define WBMENU_WN__VIEW_DEFAULT     51, "Default",           0, MENUTOGGLE|CHECKIT|CHECKED, ~((1 << 0))
#define WBMENU_WN__VIEW_ICON        52, "Icon",            "1", MENUTOGGLE|CHECKIT, ~((1 << 1))
#define WBMENU_WN__VIEW_NAME        53, "Name",            "2", MENUTOGGLE|CHECKIT, ~((1 << 2))
#define WBMENU_WN__VIEW_SIZE        54, "Size",            "3", MENUTOGGLE|CHECKIT, ~((1 << 3))
#define WBMENU_WN__VIEW_DATE        55, "Date",            "4", MENUTOGGLE|CHECKIT, ~((1 << 4))
#define WBMENU_WN__VIEW_TYPE        56, "Type",            "5", MENUTOGGLE|CHECKIT, ~((1 << 5))

/* Icon Menu */
#define WBMENU_IC               60, "Icons", 0, 0, 0
#define WBMENU_IC_OPEN              61, "Open",            "O", 0, 0
#define WBMENU_IC_COPY              62, "Copy",            "C", 0, 0
#define WBMENU_IC_RENAME            63, "Rename...",       "R", 0, 0
#define WBMENU_IC_INFO              64, "Information...",  "I", 0, 0
#define WBMENU_IC_SNAPSHOT          65, "Snapshot",        "S", 0, 0
#define WBMENU_IC_UNSNAPSHOT        66, "Unsnapshot",      "U", 0, 0
#define WBMENU_IC_LEAVE_OUT         67, "Leave out",       "L", 0, 0
#define WBMENU_IC_PUT_AWAY          68, "Put away",        "P", 0, 0
#define WBMENU_IC_DELETE            69, "Delete...",         0, 0, 0
#define WBMENU_IC_FORMAT            70, "Format...",         0, 0, 0
#define WBMENU_IC_EMPTY_TRASH       71, "Empty trash",       0, Broken 0, 0

#endif /* WORKBOOK_MENU_H */
