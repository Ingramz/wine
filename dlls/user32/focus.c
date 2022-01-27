/*
 * Focus and activation functions
 *
 * Copyright 1993 David Metcalfe
 * Copyright 1995 Alex Korobka
 * Copyright 1994, 2002 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "win.h"
#include "imm.h"
#include "user_private.h"
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(win);


/*****************************************************************
 *		set_focus_window
 *
 * Change the focus window, sending the WM_SETFOCUS and WM_KILLFOCUS messages
 */
static HWND set_focus_window( HWND hwnd, BOOL from_active )
{
    HWND previous = 0, ime_default;
    BOOL ret;

    SERVER_START_REQ( set_focus_window )
    {
        req->handle = wine_server_user_handle( hwnd );
        if ((ret = !wine_server_call_err( req )))
            previous = wine_server_ptr_handle( reply->previous );
    }
    SERVER_END_REQ;
    if (!ret) return 0;
    if (previous == hwnd) return previous;

    if (previous)
    {
        if (!IsWindow(hwnd) && !from_active)
            NotifyWinEvent( EVENT_OBJECT_FOCUS, previous, OBJID_CLIENT, CHILDID_SELF );

        SendMessageW( previous, WM_KILLFOCUS, (WPARAM)hwnd, 0 );

        ime_default = ImmGetDefaultIMEWnd( previous );
        if (ime_default)
            SendMessageW( ime_default, WM_IME_INTERNAL, IME_INTERNAL_DEACTIVATE, (LPARAM)previous );

        if (hwnd != GetFocus()) return previous;  /* changed by the message */
    }
    if (IsWindow(hwnd))
    {
        USER_Driver->pSetFocus(hwnd);
        if (!from_active)
            NotifyWinEvent( EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, CHILDID_SELF );

        ime_default = ImmGetDefaultIMEWnd( hwnd );
        if (ime_default)
            SendMessageW( ime_default, WM_IME_INTERNAL, IME_INTERNAL_ACTIVATE, (LPARAM)hwnd );

        SendMessageW( hwnd, WM_SETFOCUS, (WPARAM)previous, 0 );
    }
    return previous;
}


/*******************************************************************
 *		set_active_window
 */
static BOOL set_active_window( HWND hwnd, HWND *prev, BOOL mouse, BOOL focus )
{
    HWND previous = GetActiveWindow();
    BOOL ret = FALSE;
    DWORD old_thread, new_thread;
    CBTACTIVATESTRUCT cbt;

    if (previous == hwnd)
    {
        if (prev) *prev = hwnd;
        return TRUE;
    }

    if (prev) *prev = previous;
    if (win_set_flags( hwnd, WIN_IS_ACTIVATING, 0 ) & WIN_IS_ACTIVATING) return TRUE;

    /* call CBT hook chain */
    cbt.fMouse     = mouse;
    cbt.hWndActive = previous;
    if (HOOK_CallHooks( WH_CBT, HCBT_ACTIVATE, (WPARAM)hwnd, (LPARAM)&cbt, TRUE )) goto done;

    if (IsWindow(previous))
    {
        SendMessageW( previous, WM_NCACTIVATE, FALSE, (LPARAM)hwnd );
        SendMessageW( previous, WM_ACTIVATE,
                      MAKEWPARAM( WA_INACTIVE, IsIconic(previous) ), (LPARAM)hwnd );
    }

    SERVER_START_REQ( set_active_window )
    {
        req->handle = wine_server_user_handle( hwnd );
        if ((ret = !wine_server_call_err( req )))
            previous = wine_server_ptr_handle( reply->previous );
    }
    SERVER_END_REQ;
    if (!ret) goto done;
    if (prev) *prev = previous;
    if (previous == hwnd) goto done;

    if (hwnd)
    {
        /* send palette messages */
        if (SendMessageW( hwnd, WM_QUERYNEWPALETTE, 0, 0 ))
            SendMessageTimeoutW( HWND_BROADCAST, WM_PALETTEISCHANGING, (WPARAM)hwnd, 0,
                                 SMTO_ABORTIFHUNG, 2000, NULL );
        if (!(ret = IsWindow( hwnd ))) goto done;
    }

    old_thread = previous ? GetWindowThreadProcessId( previous, NULL ) : 0;
    new_thread = hwnd ? GetWindowThreadProcessId( hwnd, NULL ) : 0;

    if (old_thread != new_thread)
    {
        HWND *list, *phwnd;

        if ((list = WIN_ListChildren( GetDesktopWindow() )))
        {
            if (old_thread)
            {
                for (phwnd = list; *phwnd; phwnd++)
                {
                    if (GetWindowThreadProcessId( *phwnd, NULL ) == old_thread)
                        SendMessageW( *phwnd, WM_ACTIVATEAPP, 0, new_thread );
                }
            }
            if (new_thread)
            {
                for (phwnd = list; *phwnd; phwnd++)
                {
                    if (GetWindowThreadProcessId( *phwnd, NULL ) == new_thread)
                        SendMessageW( *phwnd, WM_ACTIVATEAPP, 1, old_thread );
                }
            }
            HeapFree( GetProcessHeap(), 0, list );
        }
    }

    if (IsWindow(hwnd))
    {
        SendMessageW( hwnd, WM_NCACTIVATE,
                      (hwnd == GetForegroundWindow()) && !(win_get_flags(previous) & WIN_IS_ACTIVATING),
                      (LPARAM)previous );
        SendMessageW( hwnd, WM_ACTIVATE,
                      MAKEWPARAM( mouse ? WA_CLICKACTIVE : WA_ACTIVE, IsIconic(hwnd) ),
                      (LPARAM)previous );
        if (GetAncestor( hwnd, GA_PARENT ) == GetDesktopWindow())
            PostMessageW( GetDesktopWindow(), WM_PARENTNOTIFY, WM_NCACTIVATE, (LPARAM)hwnd );
    }

    /* now change focus if necessary */
    if (focus)
    {
        GUITHREADINFO info;

        info.cbSize = sizeof(info);
        GetGUIThreadInfo( GetCurrentThreadId(), &info );
        /* Do not change focus if the window is no more active */
        if (hwnd == info.hwndActive)
        {
            if (!info.hwndFocus || !hwnd || GetAncestor( info.hwndFocus, GA_ROOT ) != hwnd)
                set_focus_window( hwnd, TRUE );
        }
    }

done:
    win_set_flags( hwnd, 0, WIN_IS_ACTIVATING );
    return ret;
}


/*******************************************************************
 *		set_foreground_window
 */
static BOOL set_foreground_window( HWND hwnd, BOOL mouse )
{
    BOOL ret, send_msg_old = FALSE, send_msg_new = FALSE;
    HWND previous = 0;

    SERVER_START_REQ( set_foreground_window )
    {
        req->handle = wine_server_user_handle( hwnd );
        if ((ret = !wine_server_call_err( req )))
        {
            previous = wine_server_ptr_handle( reply->previous );
            send_msg_old = reply->send_msg_old;
            send_msg_new = reply->send_msg_new;
        }
    }
    SERVER_END_REQ;

    if (ret && previous != hwnd)
    {
        if (send_msg_old)  /* old window belongs to other thread */
            SendNotifyMessageW( previous, WM_WINE_SETACTIVEWINDOW, 0, 0 );
        else if (send_msg_new)  /* old window belongs to us but new one to other thread */
            ret = set_active_window( 0, NULL, mouse, TRUE );

        if (send_msg_new)  /* new window belongs to other thread */
            SendNotifyMessageW( hwnd, WM_WINE_SETACTIVEWINDOW, (WPARAM)hwnd, 0 );
        else  /* new window belongs to us */
            ret = set_active_window( hwnd, NULL, mouse, TRUE );
    }
    return ret;
}


/*******************************************************************
 *		FOCUS_MouseActivate
 *
 * Activate a window as a result of a mouse click
 */
BOOL FOCUS_MouseActivate( HWND hwnd )
{
    return set_foreground_window( hwnd, TRUE );
}


/*******************************************************************
 *		SetActiveWindow (USER32.@)
 */
HWND WINAPI SetActiveWindow( HWND hwnd )
{
    HWND prev;

    TRACE( "%p\n", hwnd );

    if (hwnd)
    {
        LONG style;

        hwnd = WIN_GetFullHandle( hwnd );
        if (!IsWindow( hwnd ))
        {
            SetLastError( ERROR_INVALID_WINDOW_HANDLE );
            return 0;
        }

        style = GetWindowLongW( hwnd, GWL_STYLE );
        if ((style & (WS_POPUP|WS_CHILD)) == WS_CHILD)
            return GetActiveWindow();  /* Windows doesn't seem to return an error here */
    }

    if (!set_active_window( hwnd, &prev, FALSE, TRUE )) return 0;
    return prev;
}


/*****************************************************************
 *		SetFocus  (USER32.@)
 */
HWND WINAPI SetFocus( HWND hwnd )
{
    HWND hwndTop = hwnd;
    HWND previous = GetFocus();

    TRACE( "%p prev %p\n", hwnd, previous );

    if (hwnd)
    {
        /* Check if we can set the focus to this window */
        hwnd = WIN_GetFullHandle( hwnd );
        if (!IsWindow( hwnd ))
        {
            SetLastError( ERROR_INVALID_WINDOW_HANDLE );
            return 0;
        }
        if (hwnd == previous) return previous;  /* nothing to do */
        for (;;)
        {
            HWND parent;
            LONG style = GetWindowLongW( hwndTop, GWL_STYLE );
            if (style & (WS_MINIMIZE | WS_DISABLED)) return 0;
            if (!(style & WS_CHILD)) break;
            parent = GetAncestor( hwndTop, GA_PARENT );
            if (!parent || parent == GetDesktopWindow())
            {
                if ((style & (WS_POPUP|WS_CHILD)) == WS_CHILD) return 0;
                break;
            }
            if (parent == get_hwnd_message_parent()) return 0;
            hwndTop = parent;
        }

        /* call hooks */
        if (HOOK_CallHooks( WH_CBT, HCBT_SETFOCUS, (WPARAM)hwnd, (LPARAM)previous, TRUE )) return 0;

        /* activate hwndTop if needed. */
        if (hwndTop != GetActiveWindow())
        {
            if (!set_active_window( hwndTop, NULL, FALSE, FALSE )) return 0;
            if (!IsWindow( hwnd )) return 0;  /* Abort if window destroyed */

            /* Do not change focus if the window is no longer active */
            if (hwndTop != GetActiveWindow()) return 0;
        }
    }
    else /* NULL hwnd passed in */
    {
        if (!previous) return 0;  /* nothing to do */
        if (HOOK_CallHooks( WH_CBT, HCBT_SETFOCUS, 0, (LPARAM)previous, TRUE )) return 0;
    }

    /* change focus and send messages */
    return set_focus_window( hwnd, FALSE );
}


/*******************************************************************
 *		SetForegroundWindow  (USER32.@)
 */
BOOL WINAPI SetForegroundWindow( HWND hwnd )
{
    TRACE( "%p\n", hwnd );

    hwnd = WIN_GetFullHandle( hwnd );
    return set_foreground_window( hwnd, FALSE );
}


/*******************************************************************
 *		GetActiveWindow  (USER32.@)
 */
HWND WINAPI GetActiveWindow(void)
{
    volatile struct input_shared_memory *shared = get_input_shared_memory();
    HWND ret = 0;

    if (!shared) return 0;
    SHARED_READ_BEGIN( &shared->seq )
    {
        ret = wine_server_ptr_handle( shared->active );
    }
    SHARED_READ_END( &shared->seq );
    return ret;
}


/*****************************************************************
 *		GetFocus  (USER32.@)
 */
HWND WINAPI GetFocus(void)
{
    volatile struct input_shared_memory *shared = get_input_shared_memory();
    HWND ret = 0;

    if (!shared) return 0;
    SHARED_READ_BEGIN( &shared->seq )
    {
        ret = wine_server_ptr_handle( shared->focus );
    }
    SHARED_READ_END( &shared->seq );
    return ret;
}


/*******************************************************************
 *		GetForegroundWindow  (USER32.@)
 */
HWND WINAPI GetForegroundWindow(void)
{
    volatile struct input_shared_memory *shared = get_foreground_shared_memory();
    HWND ret = 0;

    if (!shared) return 0;
    SHARED_READ_BEGIN( &shared->seq )
    {
        ret = wine_server_ptr_handle( shared->active );
    }
    SHARED_READ_END( &shared->seq );
    return ret;
}


/***********************************************************************
*		SetShellWindowEx (USER32.@)
* hwndShell =    Progman[Program Manager]
*                |-> SHELLDLL_DefView
* hwndListView = |   |-> SysListView32
*                |   |   |-> tooltips_class32
*                |   |
*                |   |-> SysHeader32
*                |
*                |-> ProxyTarget
*/
BOOL WINAPI SetShellWindowEx(HWND hwndShell, HWND hwndListView)
{
    BOOL ret;

    if (GetShellWindow())
        return FALSE;

    if (GetWindowLongW(hwndShell, GWL_EXSTYLE) & WS_EX_TOPMOST)
        return FALSE;

    if (hwndListView != hwndShell)
        if (GetWindowLongW(hwndListView, GWL_EXSTYLE) & WS_EX_TOPMOST)
            return FALSE;

    if (hwndListView && hwndListView!=hwndShell)
        SetWindowPos(hwndListView, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

    SetWindowPos(hwndShell, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

    SERVER_START_REQ(set_global_windows)
    {
        req->flags          = SET_GLOBAL_SHELL_WINDOWS;
        req->shell_window   = wine_server_user_handle( hwndShell );
        req->shell_listview = wine_server_user_handle( hwndListView );
        ret = !wine_server_call_err(req);
    }
    SERVER_END_REQ;

    return ret;
}


/*******************************************************************
*		SetShellWindow (USER32.@)
*/
BOOL WINAPI SetShellWindow(HWND hwndShell)
{
    return SetShellWindowEx(hwndShell, hwndShell);
}


/*******************************************************************
*		GetShellWindow (USER32.@)
*/
HWND WINAPI GetShellWindow(void)
{
    HWND hwndShell = 0;

    SERVER_START_REQ(set_global_windows)
    {
        req->flags = 0;
        if (!wine_server_call_err(req))
            hwndShell = wine_server_ptr_handle( reply->old_shell_window );
    }
    SERVER_END_REQ;

    return hwndShell;
}


/***********************************************************************
 *		SetProgmanWindow (USER32.@)
 */
HWND WINAPI SetProgmanWindow ( HWND hwnd )
{
    SERVER_START_REQ(set_global_windows)
    {
        req->flags          = SET_GLOBAL_PROGMAN_WINDOW;
        req->progman_window = wine_server_user_handle( hwnd );
        if (wine_server_call_err( req )) hwnd = 0;
    }
    SERVER_END_REQ;
    return hwnd;
}


/***********************************************************************
 *		GetProgmanWindow (USER32.@)
 */
HWND WINAPI GetProgmanWindow(void)
{
    HWND ret = 0;

    SERVER_START_REQ(set_global_windows)
    {
        req->flags = 0;
        if (!wine_server_call_err(req))
            ret = wine_server_ptr_handle( reply->old_progman_window );
    }
    SERVER_END_REQ;
    return ret;
}


/***********************************************************************
 *		SetTaskmanWindow (USER32.@)
 * NOTES
 *   hwnd = MSTaskSwWClass
 *          |-> SysTabControl32
 */
HWND WINAPI SetTaskmanWindow ( HWND hwnd )
{
    SERVER_START_REQ(set_global_windows)
    {
        req->flags          = SET_GLOBAL_TASKMAN_WINDOW;
        req->taskman_window = wine_server_user_handle( hwnd );
        if (wine_server_call_err( req )) hwnd = 0;
    }
    SERVER_END_REQ;
    return hwnd;
}

/***********************************************************************
 *		GetTaskmanWindow (USER32.@)
 */
HWND WINAPI GetTaskmanWindow(void)
{
    HWND ret = 0;

    SERVER_START_REQ(set_global_windows)
    {
        req->flags = 0;
        if (!wine_server_call_err(req))
            ret = wine_server_ptr_handle( reply->old_taskman_window );
    }
    SERVER_END_REQ;
    return ret;
}
