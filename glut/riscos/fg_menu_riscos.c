/*
 * fg_menu_riscos.c
 *
 * The RISC OS back end of freeglut: GLUT menus shown as Wimp menus.
 *
 * freeglut keeps building its menu trees as usual (glutCreateMenu,
 * glutAddMenuEntry, glutAddSubMenu...). When a button with a menu attached
 * is clicked, the tree is turned into a Wimp menu and opened at the
 * pointer; a choice calls the menu's callback with the entry's value, as
 * GLUT does. Choosing with Adjust keeps the menu open (the RISC OS way).
 *
 * Copyright (c) 2026 Andrew Youll. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <kernel.h>
#include <swis.h>
#include <GL/freeglut.h>
#include "../fg_internal.h"

#define Wimp_CreateMenu_        0x400D4
#define Wimp_GetPointerInfo_    0x400CF

extern void fghPlatformGetCursorPos( const SFG_Window *window, GLboolean client,
                                     SFG_XYUse *mouse_pos );

#define MENU_DEPTH  8                   /* submenus deeper than this are left out */

/* The Wimp menu blocks of the open menu tree (one malloc'd block each) */
static void **fghBlocks;
static int fghNumBlocks, fghMaxBlocks;

static int fghOpenMenuID;               /* the GLUT menu that is open, or 0 */
static int fghOpenWindowID;             /* the window it was opened from */
static int fghOpenX, fghOpenY;          /* where (OS units), for reopening */

static void fghFreeBlocks( void )
{
    int i;
    for( i = 0; i < fghNumBlocks; i++ )
        free( fghBlocks[ i ] );
    fghNumBlocks = 0;
}

static void *fghNewBlock( size_t size )
{
    void *p;
    if( fghNumBlocks == fghMaxBlocks )
    {
        int n = fghMaxBlocks ? fghMaxBlocks * 2 : 16;
        void **b = realloc( fghBlocks, n * sizeof *b );
        if( !b )
            return NULL;
        fghBlocks = b;
        fghMaxBlocks = n;
    }
    p = calloc( 1, size );
    if( p )
        fghBlocks[ fghNumBlocks++ ] = p;
    return p;
}

/* Build the Wimp menu for a GLUT menu (and its submenus). */
static int *fghBuildMenu( SFG_Menu *menu, const char *title, int depth )
{
    SFG_MenuEntry *entry;
    int n = 0, i, width, *block, *item;
    size_t longest = strlen( title );

    for( entry = (SFG_MenuEntry *) menu->Entries.First; entry;
         entry = (SFG_MenuEntry *) entry->Node.Next )
    {
        size_t len = entry->Text ? strlen( entry->Text ) : 0;
        if( len > longest )
            longest = len;
        n++;
    }
    if( n == 0 )
        return NULL;

    block = fghNewBlock( 28 + 24 * n );
    if( !block )
        return NULL;
    width = (int) ( longest + 2 ) * 16;
    if( width < 96 )
        width = 96;
    /* header: indirected title (see the first item's flags) */
    block[0] = (int) title;
    block[1] = -1;
    block[2] = (int) strlen( title ) + 1;
    ( (unsigned char *) block )[12] = 7;        /* title fg: black */
    ( (unsigned char *) block )[13] = 2;        /* title bg: grey */
    ( (unsigned char *) block )[14] = 7;        /* entries fg: black */
    ( (unsigned char *) block )[15] = 0;        /* entries bg: white */
    block[4] = width;
    block[5] = 44;                              /* entry height */
    block[6] = 0;                               /* gap */

    item = block + 7;
    for( i = 0, entry = (SFG_MenuEntry *) menu->Entries.First; entry;
         i++, entry = (SFG_MenuEntry *) entry->Node.Next, item += 6 )
    {
        const char *text = entry->Text ? entry->Text : "";
        int *sub = NULL;

        if( entry->SubMenu && depth < MENU_DEPTH )
            sub = fghBuildMenu( entry->SubMenu, text, depth + 1 );
        item[0] = ( i == 0 ? 0x100 : 0 ) | ( i == n - 1 ? 0x80 : 0 );
        item[1] = sub ? (int) sub : -1;
        /* text, vertically centred, filled, indirected; black on white;
           shaded if it leads to an empty submenu */
        item[2] = 0x07000131 | ( entry->SubMenu && !sub ? 1 << 22 : 0 );
        item[3] = (int) text;
        item[4] = -1;
        item[5] = (int) strlen( text ) + 1;
    }
    return block;
}

static void fghMenuStatus( SFG_Window *window, SFG_Menu *menu, int status )
{
    if( !fgState.MenuStateCallback && !fgState.MenuStatusCallback )
        return;
    fgStructure.CurrentMenu = menu;
    if( window )
        fgSetWindow( window );
    if( fgState.MenuStateCallback )
        fgState.MenuStateCallback( status );
    if( fgState.MenuStatusCallback )
    {
        SFG_XYUse pos;
        fghPlatformGetCursorPos( window, GL_TRUE, &pos );
        fgState.MenuStatusCallback( status, pos.X, pos.Y, fgState.MenuStatusCallbackData );
    }
}

static int fghCreate( SFG_Menu *menu, int x, int y )
{
    _kernel_swi_regs r;
    int *block;

    fghFreeBlocks( );
    block = fghBuildMenu( menu, fgDisplay.pDisplay.TaskName, 0 );
    if( !block )
        return 0;
    r.r[1] = (int) block;
    r.r[2] = x;
    r.r[3] = y;
    return _kernel_swi( Wimp_CreateMenu_, &r, &r ) == NULL;
}

/* Open a GLUT menu at the pointer (x, y in OS units). */
int fghRiscosOpenMenu( SFG_Window *window, SFG_Menu *menu, int x, int y )
{
    if( fghOpenMenuID )
        fghRiscosMenusDeleted( );
    if( !fghCreate( menu, x - 64, y ) )
        return 0;
    fghOpenMenuID = menu->ID;
    fghOpenWindowID = window->ID;
    fghOpenX = x - 64;
    fghOpenY = y;
    fghMenuStatus( window, menu, GLUT_MENU_IN_USE );
    return 1;
}

/* Menu_Selection: items is the list of choices, ending with -1. */
void fghRiscosMenuSelection( const int *items )
{
    SFG_Menu *menu = fgMenuByID( fghOpenMenuID ), *root = menu;
    SFG_Window *window = fgWindowByID( fghOpenWindowID );
    SFG_MenuEntry *entry = NULL;
    int block[5], i, adjust = 0;
    _kernel_swi_regs r;

    if( !menu )
        return;
    for( i = 0; items[ i ] >= 0 && menu; i++ )
    {
        int k = items[ i ];
        for( entry = (SFG_MenuEntry *) menu->Entries.First; entry && k > 0;
             entry = (SFG_MenuEntry *) entry->Node.Next )
            k--;
        if( !entry )
            return;
        if( items[ i + 1 ] >= 0 )
            menu = entry->SubMenu;
    }
    r.r[1] = (int) block;
    if( _kernel_swi( Wimp_GetPointerInfo_, &r, &r ) == NULL )
        adjust = ( block[2] & 1 ) != 0;

    if( entry && menu && !entry->SubMenu && window )
    {
        SFG_Window *save_window = fgStructure.CurrentWindow;
        SFG_Menu *save_menu = fgStructure.CurrentMenu;
        int id = fghOpenMenuID;

        fgSetWindow( window );
        fgStructure.CurrentMenu = menu;
        menu->Callback( entry->ID, menu->CallbackData );
        if( fgStructure.Windows.First )
        {
            if( save_window && fgWindowByID( save_window->ID ) == save_window )
                fgSetWindow( save_window );
            fgStructure.CurrentMenu = save_menu;
        }
        /* the callback may have destroyed the menu or the window */
        root = fgMenuByID( id );
        window = fgWindowByID( fghOpenWindowID );
    }

    if( adjust && root && window && fghCreate( root, fghOpenX, fghOpenY ) )
        return;                         /* still open */
    fghOpenMenuID = 0;
    fghFreeBlocks( );
    fghMenuStatus( window, root, GLUT_MENU_NOT_IN_USE );
}

/* The menu tree was closed without a choice (Message_MenusDeleted). */
void fghRiscosMenusDeleted( void )
{
    SFG_Menu *menu;
    SFG_Window *window;

    if( !fghOpenMenuID )
        return;
    menu = fgMenuByID( fghOpenMenuID );
    window = fgWindowByID( fghOpenWindowID );
    fghOpenMenuID = 0;
    fghFreeBlocks( );
    fghMenuStatus( window, menu, GLUT_MENU_NOT_IN_USE );
}
