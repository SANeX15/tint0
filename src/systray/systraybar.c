/**************************************************************************
* Tint2 : systraybar
*
* Copyright (C) 2009 thierry lorthiois (lorthiois@bbsoft.fr) from Omega distribution
* based on 'docker-1.5' from Ben Jansens.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License version 2
* as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <Imlib2.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>


#include "systraybar.h"
#include "server.h"
#include "panel.h"

GSList *icons;

/* defined in the systray spec */
#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

// selection window
Window net_sel_win = None;

// freedesktop specification doesn't allow multi systray
Systraybar systray;
int refresh_systray;
int systray_enabled;
int systray_max_icon_size;
int systray_monitor;
int chrono;
int systray_composited;
int systray_profile;
// background pixmap if we render ourselves the icons
static Pixmap render_background;

const int min_refresh_period = 50;
const int max_fast_refreshes = 5;

void default_systray()
{
	memset(&systray, 0, sizeof(Systraybar));
	render_background = 0;
	chrono = 0;
	systray.alpha = 100;
	systray.sort = SYSTRAY_SORT_LEFT2RIGHT;
	systray.area._draw_foreground = draw_systray;
	systray.area._on_change_layout = on_change_systray;
	systray.area.size_mode = SIZE_BY_CONTENT;
	systray.area._resize = resize_systray;
	systray_profile = getenv("SYSTRAY_PROFILING") != NULL;
}

void cleanup_systray()
{
	stop_net();
	systray_enabled = 0;
	systray_max_icon_size = 0;
	systray_monitor = 0;
	systray.area.on_screen = 0;
	free_area(&systray.area);
	if (render_background) {
		XFreePixmap(server.dsp, render_background);
		render_background = 0;
	}
}

void init_systray()
{
	if (!systray_enabled)
		return;

	systray_composited = !server.disable_transparency && server.visual32 && server.colormap32;
	fprintf(stderr, "Systray composited rendering %s\n", systray_composited ? "on" : "off");

	if (!systray_composited) {
		fprintf(stderr, "systray_asb forced to 100 0 0\n");
		systray.alpha = 100;
		systray.brightness = systray.saturation = 0;
	}

	start_net();
}


void init_systray_panel(void *p)
{
	systray.area.parent = p;
	systray.area.panel = p;
	if (!systray.area.bg)
		systray.area.bg = &g_array_index(backgrounds, Background, 0);

	GSList *l;
	int count = 0;
	for (l = systray.list_icons; l ; l = l->next) {
		if (((TrayWindow*)l->data)->hide)
			continue;
		count++;
	}
	if (count == 0)
		hide(&systray.area);
	else
		show(&systray.area);
	refresh_systray = 0;
}


void draw_systray(void *obj, cairo_t *c)
{
	if (systray_profile)
		fprintf(stderr, BLUE "[%f] %s:%d\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
	if (systray_composited) {
		if (render_background)
			XFreePixmap(server.dsp, render_background);
		render_background = XCreatePixmap(server.dsp, server.root_win, systray.area.width, systray.area.height, server.depth);
		XCopyArea(server.dsp, systray.area.pix, render_background, server.gc, 0, 0, systray.area.width, systray.area.height, 0, 0);
	}

	refresh_systray = 1;
}


int resize_systray(void *obj)
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);
	Systraybar *sysbar = obj;
	GSList *l;
	int count;

	if (panel_horizontal)
		sysbar->icon_size = sysbar->area.height;
	else
		sysbar->icon_size = sysbar->area.width;
	sysbar->icon_size = sysbar->icon_size - (2 * sysbar->area.bg->border.width) - (2 * sysbar->area.paddingy);
	if (systray_max_icon_size > 0 && sysbar->icon_size > systray_max_icon_size)
		sysbar->icon_size = systray_max_icon_size;
	count = 0;
	for (l = systray.list_icons; l ; l = l->next) {
		if (((TrayWindow*)l->data)->hide)
			continue;
		count++;
	}
	fprintf(stderr, BLUE "%s:%d number of icons = %d\n" RESET, __FUNCTION__, __LINE__, count);

	if (panel_horizontal) {
		int height = sysbar->area.height - 2*sysbar->area.bg->border.width - 2*sysbar->area.paddingy;
		// here icons_per_column always higher than 0
		sysbar->icons_per_column = (height+sysbar->area.paddingx) / (sysbar->icon_size+sysbar->area.paddingx);
		sysbar->marging = height - (sysbar->icons_per_column-1)*(sysbar->icon_size+sysbar->area.paddingx) - sysbar->icon_size;
		sysbar->icons_per_row = count / sysbar->icons_per_column + (count%sysbar->icons_per_column != 0);
		systray.area.width = (2 * systray.area.bg->border.width) + (2 * systray.area.paddingxlr) + (sysbar->icon_size * sysbar->icons_per_row) + ((sysbar->icons_per_row-1) * systray.area.paddingx);
	} else {
		int width = sysbar->area.width - 2*sysbar->area.bg->border.width - 2*sysbar->area.paddingy;
		// here icons_per_row always higher than 0
		sysbar->icons_per_row = (width+sysbar->area.paddingx) / (sysbar->icon_size+sysbar->area.paddingx);
		sysbar->marging = width - (sysbar->icons_per_row-1)*(sysbar->icon_size+sysbar->area.paddingx) - sysbar->icon_size;
		sysbar->icons_per_column = count / sysbar->icons_per_row+ (count%sysbar->icons_per_row != 0);
		systray.area.height = (2 * systray.area.bg->border.width) + (2 * systray.area.paddingxlr) + (sysbar->icon_size * sysbar->icons_per_column) + ((sysbar->icons_per_column-1) * systray.area.paddingx);
	}
	return 1;
}


void on_change_systray (void *obj)
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);
	// here, systray.area.posx/posy are defined by rendering engine. so we can calculate position of tray icon.
	Systraybar *sysbar = obj;
	if (sysbar->icons_per_column == 0 || sysbar->icons_per_row == 0)
		return;

	Panel *panel = sysbar->area.panel;
	int i, posx, posy;
	int start = panel->area.bg->border.width + panel->area.paddingy + systray.area.bg->border.width + systray.area.paddingy + sysbar->marging/2;
	if (panel_horizontal) {
		posy = start;
		posx = systray.area.posx + systray.area.bg->border.width + systray.area.paddingxlr;
	} else {
		posx = start;
		posy = systray.area.posy + systray.area.bg->border.width + systray.area.paddingxlr;
	}

	TrayWindow *traywin;
	GSList *l;
	for (i = 1, l = systray.list_icons; l ; i++, l = l->next) {
		traywin = (TrayWindow*)l->data;
		if (traywin->hide)
			continue;

		traywin->y = posy;
		traywin->x = posx;
		if (systray_profile)
			fprintf(stderr, "%s:%d win = %lu (%s), parent = %lu, x = %d, y = %d\n", __FUNCTION__, __LINE__, traywin->win, traywin->name, traywin->parent, posx, posy);
		traywin->width = sysbar->icon_size;
		traywin->height = sysbar->icon_size;
		if (panel_horizontal) {
			if (i % sysbar->icons_per_column) {
				posy += sysbar->icon_size + sysbar->area.paddingx;
			} else {
				posy = start;
				posx += (sysbar->icon_size + systray.area.paddingx);
			}
		} else {
			if (i % sysbar->icons_per_row) {
				posx += sysbar->icon_size + systray.area.paddingx;
			} else {
				posx = start;
				posy += (sysbar->icon_size + systray.area.paddingx);
			}
		}

		// position and size the icon window
		XMoveResizeWindow(server.dsp, traywin->parent, traywin->x, traywin->y, traywin->width, traywin->height);
		if (traywin->reparented) {
			XMoveResizeWindow(server.dsp, traywin->win, 0, 0, traywin->width, traywin->height);
		}
	}
	refresh_systray = 1;
}


// ***********************************************
// systray protocol

void start_net()
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);
	if (net_sel_win) {
		// protocol already started
		if (!systray_enabled)
			stop_net();
		return;
	} else {
		if (!systray_enabled)
			return;
	}

	Window win = XGetSelectionOwner(server.dsp, server.atom._NET_SYSTEM_TRAY_SCREEN);

	// freedesktop systray specification
	if (win != None) {
		// search pid
		Atom _NET_WM_PID, actual_type;
		int actual_format;
		unsigned long nitems;
		unsigned long bytes_after;
		unsigned char *prop = 0;
		int pid;

		_NET_WM_PID = XInternAtom(server.dsp, "_NET_WM_PID", True);
		int ret = XGetWindowProperty(server.dsp, win, _NET_WM_PID, 0, 1024, False, AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &prop);

		fprintf(stderr, RED "tint2 : another systray is running");
		if (ret == Success && prop) {
			pid = prop[1] * 256;
			pid += prop[0];
			fprintf(stderr, " pid=%d", pid);
		}
		fprintf(stderr, "\n" RESET);
		return;
	}

	// init systray protocol
	net_sel_win = XCreateSimpleWindow(server.dsp, server.root_win, -1, -1, 1, 1, 0, 0, 0);

	// v0.3 trayer specification. tint2 always horizontal.
	// Vertical panel will draw the systray horizontal.
	long orient = 0;
	XChangeProperty(server.dsp, net_sel_win, server.atom._NET_SYSTEM_TRAY_ORIENTATION, XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &orient, 1);
	VisualID vid;
	if (systray_composited)
		vid = XVisualIDFromVisual(server.visual32);
	else
		vid = XVisualIDFromVisual(server.visual);
	XChangeProperty(server.dsp, net_sel_win, XInternAtom(server.dsp, "_NET_SYSTEM_TRAY_VISUAL", False), XA_VISUALID, 32, PropModeReplace, (unsigned char*)&vid, 1);

	XSetSelectionOwner(server.dsp, server.atom._NET_SYSTEM_TRAY_SCREEN, net_sel_win, CurrentTime);
	if (XGetSelectionOwner(server.dsp, server.atom._NET_SYSTEM_TRAY_SCREEN) != net_sel_win) {
		stop_net();
		fprintf(stderr, RED "tint2 : can't get systray manager\n" RESET);
		return;
	}

	fprintf(stderr, GREEN "tint2 : systray started\n" RESET);
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);
	XClientMessageEvent ev;
	ev.type = ClientMessage;
	ev.window = server.root_win;
	ev.message_type = server.atom.MANAGER;
	ev.format = 32;
	ev.data.l[0] = CurrentTime;
	ev.data.l[1] = server.atom._NET_SYSTEM_TRAY_SCREEN;
	ev.data.l[2] = net_sel_win;
	ev.data.l[3] = 0;
	ev.data.l[4] = 0;
	XSendEvent(server.dsp, server.root_win, False, StructureNotifyMask, (XEvent*)&ev);
}


void net_message(XClientMessageEvent *e)
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);
	unsigned long opcode;
	Window win;

	opcode = e->data.l[1];
	switch (opcode) {
	case SYSTEM_TRAY_REQUEST_DOCK:
		win = e->data.l[2];
		if (win)
			add_icon(win);
		break;

	case SYSTEM_TRAY_BEGIN_MESSAGE:
	case SYSTEM_TRAY_CANCEL_MESSAGE:
		// we don't show baloons messages.
		break;

	default:
		if (opcode == server.atom._NET_SYSTEM_TRAY_MESSAGE_DATA)
			fprintf(stderr, "message from dockapp: %s\n", e->data.b);
		else
			fprintf(stderr, RED "SYSTEM_TRAY : unknown message type\n" RESET);
		break;
	}
}


void stop_net()
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);
	if (systray.list_icons) {
		// remove_icon change systray.list_icons
		while(systray.list_icons)
			remove_icon((TrayWindow*)systray.list_icons->data);

		g_slist_free(systray.list_icons);
		systray.list_icons = NULL;
	}

	if (net_sel_win != None) {
		XDestroyWindow(server.dsp, net_sel_win);
		net_sel_win = None;
	}
}


gboolean error;
int window_error_handler(Display *d, XErrorEvent *e)
{
	if (systray_profile)
		fprintf(stderr, RED "[%f] %s:%d\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
	error = TRUE;
	if (e->error_code != BadWindow) {
		fprintf(stderr, RED "systray: error code %d\n" RESET, e->error_code);
	}
	return 0;
}


static gint compare_traywindows(gconstpointer a, gconstpointer b)
{
	const TrayWindow * traywin_a = (TrayWindow*)a;
	const TrayWindow * traywin_b = (TrayWindow*)b;

	if (traywin_a->empty && !traywin_b->empty)
		return 1 * (systray.sort == SYSTRAY_SORT_RIGHT2LEFT ? -1 : 1);
	if (!traywin_a->empty && traywin_b->empty)
		return -1 * (systray.sort == SYSTRAY_SORT_RIGHT2LEFT ? -1 : 1);

	if (systray.sort == SYSTRAY_SORT_ASCENDING ||
		systray.sort == SYSTRAY_SORT_DESCENDING) {
		return g_ascii_strncasecmp(traywin_a->name, traywin_b->name, -1) *
				(systray.sort == SYSTRAY_SORT_ASCENDING ? 1 : -1);
	}

	if (systray.sort == SYSTRAY_SORT_LEFT2RIGHT ||
		systray.sort == SYSTRAY_SORT_RIGHT2LEFT) {
		return (traywin_a->chrono - traywin_b->chrono) *
				(systray.sort == SYSTRAY_SORT_LEFT2RIGHT ? 1 : -1);
	}

	return 0;
}


gboolean add_icon(Window win)
{
	XTextProperty xname;
	char *name;
	if (XGetWMName(server.dsp, win, &xname)) {
		name = strdup((char*)xname.value);
		XFree(xname.value);
	} else {
		name = strdup("");
	}

	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, win, name);
	Panel *panel = systray.area.panel;
	int hide = 0;

	// Get the process ID of the application that created the window
	int pid = 0;
	{
		Atom actual_type;
		int actual_format;
		unsigned long nitems;
		unsigned long bytes_after;
		unsigned char *prop = 0;
		int ret = XGetWindowProperty(server.dsp, win, server.atom._NET_WM_PID, 0, 1024, False, AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
		if (ret == Success && prop) {
			pid = prop[1] * 256;
			pid += prop[0];
		}
	}

	// Check if the application leaves behind empty icons
	GSList *l;
	int num_empty_same_pid = 0;
	for (l = systray.list_icons; l; l = l->next) {
		TrayWindow *other = (TrayWindow*)l->data;
		if (other->win == win) {
			free(name);
			return FALSE;
		}
		if (!systray_composited) {
			// Empty icon detection: we compare the contents of the icon with the contents of the panel pixmap.
			// If any pixel is different, the icon is not empty.
			imlib_context_set_visual(server.visual);
			imlib_context_set_colormap(server.colormap);
			imlib_context_set_drawable(other->win);
			Imlib_Image image = imlib_create_image_from_drawable(0, 0, 0, other->width, other->height, 1);
			if (image) {
				imlib_context_set_drawable(panel->temp_pmap);
				Imlib_Image bg = imlib_create_image_from_drawable(0, other->x, other->y, other->width, other->height, 1);
				imlib_context_set_image(bg);
				DATA32* data_bg = imlib_image_get_data_for_reading_only();
				imlib_context_set_image(image);
				imlib_image_set_has_alpha(other->depth > 24);
				DATA32* data = imlib_image_get_data_for_reading_only();
				int x, y;
				int empty = 1;
				for (x = 0; x < other->width && empty; x++) {
					for (y = 0; y < other->height && empty; y++) {
						DATA32 pixel = data[y * other->width + x];
						DATA32 a = (pixel >> 24) & 0xff;
						if (a == 0)
							continue;

						DATA32 rgb = pixel & 0xffFFff;
						DATA32 pixel_bg = data_bg[y * other->width + x];
						DATA32 rgb_bg = pixel_bg & 0xffFFff;
						if (rgb != rgb_bg) {
							fprintf(stderr, "Pixel: %x different from bg %x at pos %d %d\n", pixel, pixel_bg, x, y);
							empty = 0;
						}
					}
				}
				other->empty = empty;
				imlib_free_image_and_decache();
				imlib_context_set_image(bg);
				imlib_free_image_and_decache();
				if (systray_profile)
					fprintf(stderr, "[%f] %s:%d win = %lu (%s) empty = %d\n", profiling_get_time(), __FUNCTION__, __LINE__, other->win, other->name, other->empty);
			}
		}
		if (pid && other->pid == pid) {
			if (other->empty)
				num_empty_same_pid++;
		}
	}

	// Remove empty icons if the application leaves behind more than 1
	const int max_num_empty_same_pid = 0;
	if (num_empty_same_pid > max_num_empty_same_pid) {
		for (l = systray.list_icons; l; l = l->next) {
			if (pid && ((TrayWindow*)l->data)->pid == pid && ((TrayWindow*)l->data)->empty) {
				num_empty_same_pid++;
				fprintf(stderr, RED "Removing tray icon %lu (%s) from misbehaving application with pid=%d (too many icons)\n" RESET, ((TrayWindow*)l->data)->win, ((TrayWindow*)l->data)->name, pid);
				remove_icon((TrayWindow*)l->data);
				break;
			}
		}
	}

	// Create the parent window that will embed the icon
	XWindowAttributes attr;
	if (XGetWindowAttributes(server.dsp, win, &attr) == False) {
		free(name);
		return FALSE;
	}
	unsigned long mask = 0;
	XSetWindowAttributes set_attr;
	Visual* visual = server.visual;
	fprintf(stderr, GREEN "add_icon: %lu (%s), pid %d, %d, visual %p, colormap %lu, depth %d, width %d, height %d\n" RESET,
			win, name, pid, num_empty_same_pid, attr.visual, attr.colormap, attr.depth, attr.width, attr.height);
	if (server.disable_transparency) {
		set_attr.background_pixmap = ParentRelative;
		mask = CWBackPixmap;
		if (systray_composited || attr.depth != server.depth) {
			visual = attr.visual;
			set_attr.colormap = attr.colormap;
			mask |= CWColormap;
		}
	} else {
		if (systray_composited || attr.depth != server.depth) {
			visual = attr.visual;
			set_attr.background_pixel = 0;
			set_attr.border_pixel = 0;
			set_attr.colormap = attr.colormap;
			mask = CWColormap|CWBackPixel|CWBorderPixel;
		} else {
			set_attr.background_pixmap = ParentRelative;
			mask = CWBackPixmap;
		}
	}
	Window parent = XCreateWindow(server.dsp, panel->main_win, 0, 0, 30, 30, 0, attr.depth, InputOutput, visual, mask, &set_attr);

	// Add the icon to the list
	TrayWindow *traywin = g_new0(TrayWindow, 1);
	traywin->parent = parent;
	traywin->win = win;
	traywin->hide = hide;
	traywin->depth = attr.depth;
	// Reparenting is done at the first paint event when the window is positioned correctly over its empty background,
	// to prevent graphical corruptions in icons with fake transparency
	traywin->pid = pid;
	traywin->name = name;
	traywin->chrono = chrono;
	chrono++;

	if (systray.area.on_screen == 0)
		show(&systray.area);

	if (systray.sort == SYSTRAY_SORT_RIGHT2LEFT)
		systray.list_icons = g_slist_prepend(systray.list_icons, traywin);
	else
		systray.list_icons = g_slist_append(systray.list_icons, traywin);
	systray.list_icons = g_slist_sort(systray.list_icons, compare_traywindows);

	if (!traywin->hide && !panel->is_hidden)
		XMapRaised(server.dsp, traywin->parent);
	XSync(server.dsp, False);

	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d\n", profiling_get_time(), __FUNCTION__, __LINE__);

	// Resize and redraw the systray
	if (systray_profile)
		fprintf(stderr, BLUE "[%f] %s:%d trigger resize & redraw\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
	systray.area.resize = 1;
	systray.area.redraw = 1;
	panel->area.resize = 1;
	panel_refresh = 1;
	refresh_systray = 1;
	return TRUE;
}

gboolean reparent_icon(TrayWindow *traywin)
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
	if (traywin->reparented)
		return TRUE;

	Panel* panel = systray.area.panel;

	// Reparent
	XSync(server.dsp, False);
	error = FALSE;
	XErrorHandler old = XSetErrorHandler(window_error_handler);
	XReparentWindow(server.dsp, traywin->win, traywin->parent, 0, 0);
	XSync(server.dsp, False);
	XSetErrorHandler(old);
	if (error != FALSE) {
		fprintf(stderr, RED "systray %d: cannot embed icon for window %lu (%s) parent %lu pid %d\n" RESET, __LINE__, traywin->win, traywin->name, traywin->parent, traywin->pid);
		remove_icon(traywin);
		return FALSE;
	}

	// Watch for the icon trying to resize itself / closing again
	XSync(server.dsp, False);
	error = FALSE;
	old = XSetErrorHandler(window_error_handler);
	XSelectInput(server.dsp, traywin->win, StructureNotifyMask);
	XSync(server.dsp, False);
	XSetErrorHandler(old);
	if (error != FALSE) {
		fprintf(stderr, RED "systray %d: cannot embed icon for window %lu (%s) parent %lu pid %d\n" RESET, __LINE__, traywin->win, traywin->name, traywin->parent, traywin->pid);
		remove_icon(traywin);
		return FALSE;
	}

	// Embed into parent
	{
		XEvent e;
		e.xclient.type = ClientMessage;
		e.xclient.serial = 0;
		e.xclient.send_event = True;
		e.xclient.message_type = server.atom._XEMBED;
		e.xclient.window = traywin->win;
		e.xclient.format = 32;
		e.xclient.data.l[0] = CurrentTime;
		e.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
		e.xclient.data.l[2] = 0;
		e.xclient.data.l[3] = traywin->parent;
		e.xclient.data.l[4] = 0;
		XSync(server.dsp, False);
		error = FALSE;
		XErrorHandler old = XSetErrorHandler(window_error_handler);
		XSendEvent(server.dsp, traywin->win, False, 0xFFFFFF, &e);
		XSync(server.dsp, False);
		XSetErrorHandler(old);
		if (error != FALSE) {
			fprintf(stderr, RED "systray %d: cannot embed icon for window %lu (%s) pid %d\n" RESET, __LINE__, traywin->win, traywin->name, traywin->pid);
			remove_icon(traywin);
			return FALSE;
		}
	}

	{
		Atom acttype;
		int actfmt;
		unsigned long nbitem, bytes;
		unsigned char *data = 0;
		int ret;

		ret = XGetWindowProperty(server.dsp, traywin->win, server.atom._XEMBED_INFO, 0, 2, False, server.atom._XEMBED_INFO, &acttype, &actfmt, &nbitem, &bytes, &data);
		if (ret == Success) {
			if (data) {
				if (nbitem >= 2) {
					int hide = ((data[1] & XEMBED_MAPPED) == 0);
					if (hide) {
						// In theory we have to check the embedding with this and remove icons that refuse embedding.
						// In practice we have no idea when the other application processes the event and accepts the embed so we cannot check without a race.
						// Race can be triggered with PyGtk(2) apps.
						//fprintf(stderr, RED "tint2: window refused embedding\n" RESET);
						//remove_icon(traywin);
						//XFree(data);
						//return FALSE;
					}
				}
				XFree(data);
			}
		} else {
			fprintf(stderr, RED "tint2 : xembed error\n" RESET);
			remove_icon(traywin);
			return FALSE;
		}
	}

	// Redirect rendering when using compositing
	if (systray_composited) {
		traywin->damage = XDamageCreate(server.dsp, traywin->parent, XDamageReportRawRectangles);
		XCompositeRedirectWindow(server.dsp, traywin->parent, CompositeRedirectManual);
	}

	// Make the icon visible
	if (!traywin->hide)
		XMapWindow(server.dsp, traywin->win);
	if (!traywin->hide && !panel->is_hidden)
		XMapRaised(server.dsp, traywin->parent);
	XSync(server.dsp, False);

	XMoveResizeWindow(server.dsp, traywin->parent, traywin->x, traywin->y, traywin->width, traywin->height);
	XMoveResizeWindow(server.dsp, traywin->win, 0, 0, traywin->width, traywin->height);

	XSync(server.dsp, False);

	traywin->reparented = 1;

	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);

	return TRUE;
}

void remove_icon(TrayWindow *traywin)
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
	Panel* panel = systray.area.panel;

	// remove from our list
	systray.list_icons = g_slist_remove(systray.list_icons, traywin);
	fprintf(stderr, YELLOW "remove_icon: %lu (%s)\n" RESET, traywin->win, traywin->name);

	XSelectInput(server.dsp, traywin->win, NoEventMask);
	if (traywin->damage)
		XDamageDestroy(server.dsp, traywin->damage);

	// reparent to root
	XSync(server.dsp, False);
	error = FALSE;
	XErrorHandler old = XSetErrorHandler(window_error_handler);
	if (!traywin->hide)
		XUnmapWindow(server.dsp, traywin->win);
	XReparentWindow(server.dsp, traywin->win, server.root_win, 0, 0);
	XDestroyWindow(server.dsp, traywin->parent);
	XSync(server.dsp, False);
	XSetErrorHandler(old);
	stop_timeout(traywin->render_timeout);
	free(traywin->name);
	g_free(traywin);

	// check empty systray
	int count = 0;
	GSList *l;
	for (l = systray.list_icons; l; l = l->next) {
		if (((TrayWindow*)l->data)->hide)
			continue;
		count++;
	}
	if (count == 0)
		hide(&systray.area);

	// Resize and redraw the systray
	if (systray_profile)
		fprintf(stderr, BLUE "[%f] %s:%d trigger resize & redraw\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
	systray.area.resize = 1;
	systray.area.redraw = 1;
	panel->area.resize = 1;
	panel_refresh = 1;
	refresh_systray = 1;
}

void systray_reconfigure_event(TrayWindow *traywin, XEvent *e)
{
	if (systray_profile)
		fprintf(stderr, "XConfigure event: win = %lu (%s), x = %d, y = %d, w = %d, h = %d\n",
				traywin->win, traywin->name, e->xconfigure.x, e->xconfigure.y, e->xconfigure.width, e->xconfigure.height);
	Panel* panel = systray.area.panel;

	//fprintf(stderr, "move tray %d\n", traywin->x);
	XMoveResizeWindow(server.dsp, traywin->parent, traywin->x, traywin->y, traywin->width, traywin->height);
	if (traywin->reparented) {
		XMoveResizeWindow(server.dsp, traywin->win, 0, 0, traywin->width, traywin->height);
		// Trigger window repaint
		stop_timeout(traywin->render_timeout);
		traywin->render_timeout = add_timeout(min_refresh_period, 0, systray_render_icon, traywin, &traywin->render_timeout);
	}
	// Resize and redraw the systray
	if (systray_profile)
		fprintf(stderr, BLUE "[%f] %s:%d trigger resize & redraw\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
	systray.area.resize = 1;
	systray.area.redraw = 1;
	panel->area.resize = 1;
	panel_refresh = 1;
	refresh_systray = 1;
}

void systray_destroy_event(TrayWindow *traywin)
{
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
	remove_icon(traywin);
}


void systray_render_icon_composited(void* t)
{
	// we end up in this function only in real transparency mode or if systray_task_asb != 100 0 0
	// we made also sure, that we always have a 32 bit visual, i.e. we can safely create 32 bit pixmaps here
	TrayWindow* traywin = t;

	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);

	// wine tray icons update whenever mouse is over them, so we limit the updates to 50 ms
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	struct timespec earliest_render = add_msec_to_timespec(traywin->time_last_render, min_refresh_period);
	if (compare_timespecs(&earliest_render, &now) > min_refresh_period) {
		traywin->num_fast_renders++;
		if (traywin->num_fast_renders > max_fast_refreshes) {
			traywin->render_timeout = add_timeout(min_refresh_period, 0, systray_render_icon_composited, traywin, &traywin->render_timeout);
			if (systray_profile)
				fprintf(stderr, YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
			return;
		}
	} else {
		traywin->time_last_render.tv_sec = now.tv_sec;
		traywin->time_last_render.tv_nsec = now.tv_nsec;
		traywin->num_fast_renders = 0;
	}

	if (traywin->width == 0 || traywin->height == 0) {
		// reschedule rendering since the geometry information has not yet been processed (can happen on slow cpu)
		traywin->render_timeout = add_timeout(min_refresh_period, 0, systray_render_icon_composited, traywin, &traywin->render_timeout);
		if (systray_profile)
			fprintf(stderr, YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
		return;
	}

	if (traywin->render_timeout) {
		stop_timeout(traywin->render_timeout);
		traywin->render_timeout = NULL;
	}

	{
		// We shouldn't have to do this as we already listen for structure notify events.
		// But things work fine so why change it.
		unsigned int border_width;
		int xpos, ypos;
		unsigned int width, height, depth;
		Window root;
		if (!XGetGeometry(server.dsp, traywin->win, &root, &xpos, &ypos, &width, &height, &border_width, &depth)) {
			fprintf(stderr, RED "Couldn't get geometry of window!\n" RESET);
			return;
		}
		if (width != traywin->width || height != traywin->height || xpos != 0 || ypos != 0) {
			XMoveResizeWindow(server.dsp, traywin->win, 0, 0, traywin->width, traywin->height);
			traywin->render_timeout = add_timeout(min_refresh_period, 0, systray_render_icon_composited, traywin, &traywin->render_timeout);
			if (systray_profile)
				fprintf(stderr, YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
			return;
		}
	}

	// good systray icons support 32 bit depth, but some icons are still 24 bit.
	// We create a heuristic mask for these icons, i.e. we get the rgb value in the top left corner, and
	// mask out all pixel with the same rgb value
	Panel* panel = systray.area.panel;

	// Very ugly hack, but somehow imlib2 is not able to get the image from the traywindow itself,
	// so we first render the tray window onto a pixmap, and then we tell imlib2 to use this pixmap as
	// drawable. If someone knows why it does not work with the traywindow itself, please tell me ;)
	Pixmap tmp_pmap = XCreatePixmap(server.dsp, traywin->win, traywin->width, traywin->height, 32);
	if (!tmp_pmap) {
		goto on_systray_error;
	}
	XRenderPictFormat *f;
	if (traywin->depth == 24) {
		f = XRenderFindStandardFormat(server.dsp, PictStandardRGB24);
	} else if (traywin->depth == 32) {
		f = XRenderFindStandardFormat(server.dsp, PictStandardARGB32);
	} else {
		fprintf(stderr, RED "Strange tray icon found with depth: %d\n" RESET, traywin->depth);
		XFreePixmap(server.dsp, tmp_pmap);
		return;
	}
	XRenderPictFormat *f32 = XRenderFindVisualFormat(server.dsp, server.visual32);
	if (!f || !f32) {
		XFreePixmap(server.dsp, tmp_pmap);
		goto on_systray_error;
	}

	XSync(server.dsp, False);
	error = FALSE;
	XErrorHandler old = XSetErrorHandler(window_error_handler);

	//if (server.real_transparency)
	//Picture pict_image = XRenderCreatePicture(server.dsp, traywin->parent, f, 0, 0);
	// reverted Rev 407 because here it's breaking alls icon with systray + xcompmgr
	Picture pict_image = XRenderCreatePicture(server.dsp, traywin->win, f, 0, 0);
	if (!pict_image) {
		XFreePixmap(server.dsp, tmp_pmap);
		XSetErrorHandler(old);
		goto on_error;
	}
	Picture pict_drawable = XRenderCreatePicture(server.dsp, tmp_pmap, XRenderFindVisualFormat(server.dsp, server.visual32), 0, 0);
	if (!pict_drawable) {
		XRenderFreePicture(server.dsp, pict_image);
		XFreePixmap(server.dsp, tmp_pmap);
		XSetErrorHandler(old);
		goto on_error;
	}
	XRenderComposite(server.dsp, PictOpSrc, pict_image, None, pict_drawable, 0, 0, 0, 0, 0, 0, traywin->width, traywin->height);
	XRenderFreePicture(server.dsp, pict_image);
	XRenderFreePicture(server.dsp, pict_drawable);
	// end of the ugly hack and we can continue as before

	imlib_context_set_visual(server.visual32);
	imlib_context_set_colormap(server.colormap32);
	imlib_context_set_drawable(tmp_pmap);
	Imlib_Image image = imlib_create_image_from_drawable(0, 0, 0, traywin->width, traywin->height, 1);
	if (!image) {
		imlib_context_set_visual(server.visual);
		imlib_context_set_colormap(server.colormap);
		XFreePixmap(server.dsp, tmp_pmap);
		XSetErrorHandler(old);
		goto on_error;
	}

	imlib_context_set_image(image);
	//if (traywin->depth == 24)
	//imlib_save_image("/home/thil77/test.jpg");
	imlib_image_set_has_alpha(1);
	DATA32* data = imlib_image_get_data();
	if (traywin->depth == 24) {
		createHeuristicMask(data, traywin->width, traywin->height);
	}

	int empty = imageEmpty(data, traywin->width, traywin->height);
	if (systray.alpha != 100 || systray.brightness != 0 || systray.saturation != 0)
		adjust_asb(data, traywin->width, traywin->height, systray.alpha, (float)systray.saturation/100, (float)systray.brightness/100);
	imlib_image_put_back_data(data);
	XCopyArea(server.dsp, render_background, systray.area.pix, server.gc, traywin->x-systray.area.posx, traywin->y-systray.area.posy, traywin->width, traywin->height, traywin->x-systray.area.posx, traywin->y-systray.area.posy);
	render_image(systray.area.pix, traywin->x-systray.area.posx, traywin->y-systray.area.posy);
	XCopyArea(server.dsp, systray.area.pix, panel->temp_pmap, server.gc, traywin->x-systray.area.posx, traywin->y-systray.area.posy, traywin->width, traywin->height, traywin->x, traywin->y);
	imlib_free_image_and_decache();
	XFreePixmap(server.dsp, tmp_pmap);
	imlib_context_set_visual(server.visual);
	imlib_context_set_colormap(server.colormap);

	if (traywin->damage)
		XDamageSubtract(server.dsp, traywin->damage, None, None);
	XSync(server.dsp, False);
	XSetErrorHandler(old);

	if (error)
		goto on_error;

	if (traywin->empty != empty) {
		traywin->empty = empty;
		systray.list_icons = g_slist_sort(systray.list_icons, compare_traywindows);
		// Resize and redraw the systray
		if (systray_profile)
			fprintf(stderr, BLUE "[%f] %s:%d trigger resize & redraw\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
		systray.area.resize = 1;
		systray.area.redraw = 1;
		panel->area.resize = 1;
		panel_refresh = 1;
		refresh_systray = 1;
	}

	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);

	return;

on_error:
	fprintf(stderr, RED "systray %d: rendering error for icon %lu (%s) pid %d\n" RESET, __LINE__, traywin->win, traywin->name, traywin->pid);
	return;

on_systray_error:
	fprintf(stderr, RED "systray %d: rendering error for icon %lu (%s) pid %d. "
		   "Disabling compositing and restarting systray...\n" RESET, __LINE__, traywin->win, traywin->name, traywin->pid);
	systray_composited = 0;
	stop_net();
	start_net();
	return;
}


void systray_render_icon(void* t)
{
	TrayWindow* traywin = t;
	if (systray_profile)
		fprintf(stderr, "[%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
	if (!traywin->reparented) {
		if (!reparent_icon(traywin))
			return;
		if (systray_composited) {
			// We need to process the events in the main loop first
			stop_timeout(traywin->render_timeout);
			traywin->render_timeout = add_timeout(min_refresh_period, 0, systray_render_icon, traywin, &traywin->render_timeout);
			if (systray_profile)
				fprintf(stderr, YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__, traywin->win, traywin->name);
			return;
		}
	}

	if (systray_composited) {
		systray_render_icon_composited(traywin);
	} else {
		// Trigger window repaint
		XClearArea(server.dsp, traywin->parent, 0, 0, traywin->width, traywin->height, True);
		XClearArea(server.dsp, traywin->win, 0, 0, traywin->width, traywin->height, True);
	}
}


void refresh_systray_icons()
{
	if (systray_profile)
		fprintf(stderr, BLUE "[%f] %s:%d\n" RESET, profiling_get_time(), __FUNCTION__, __LINE__);
	TrayWindow *traywin;
	GSList *l;
	for (l = systray.list_icons; l ; l = l->next) {
		traywin = (TrayWindow*)l->data;
		if (traywin->hide)
			continue;
		systray_render_icon(traywin);
	}
}

int systray_on_monitor(int i_monitor, int nb_panels)
{
	return (i_monitor == systray_monitor) ||
			(i_monitor == 0 && (systray_monitor >= nb_panels || systray_monitor < 0));
}
