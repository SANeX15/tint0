/**************************************************************************
*
* tint0conf
*
* Copyright (C) 2009 Thierry lorthiois (lorthiois@bbsoft.fr) from Omega distribution
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


#include "main.h"
#include "strnatcmp.h"
#include "theme_view.h"

// The data columns that we export via the tree model interface
GtkWidget *g_theme_view;
GtkListStore *g_store;
int g_width_list, g_height_list;
GtkCellRenderer *g_renderer;

gint theme_name_compare(GtkTreeModel *model,
						GtkTreeIter *a,
						GtkTreeIter *b,
						gpointer user_data);

GtkWidget *create_view()
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkWidget  *view;

	g_store = gtk_list_store_new(NB_COL, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);

	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_store));
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

	g_object_unref(g_store); // destroy store automatically with view

	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", COL_THEME_FILE);
	gtk_tree_view_column_set_visible(col, FALSE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view),col);

	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", COL_THEME_NAME);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view),col);

	g_width_list = 200;
	g_height_list = 30;
	g_renderer = gtk_cell_renderer_pixbuf_new();
	g_object_set(g_renderer, "xalign", 0.0, NULL);
	gtk_cell_renderer_set_fixed_size(g_renderer, g_width_list, g_height_list);
	// specific to gtk-2.18 or higher
	//gtk_cell_renderer_set_padding(g_renderer, 5, 5);
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(col, g_renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, g_renderer, "pixbuf", COL_SNAPSHOT);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view),col);

	GtkTreeSortable *sortable;
	sortable = GTK_TREE_SORTABLE(g_store);
	gtk_tree_sortable_set_sort_column_id(sortable, COL_THEME_FILE, GTK_SORT_ASCENDING);
	gtk_tree_sortable_set_sort_func(sortable, COL_THEME_FILE, theme_name_compare, NULL, NULL);
	return view;
}

gint theme_name_compare(GtkTreeModel *model,
						GtkTreeIter *a,
						GtkTreeIter *b,
						gpointer user_data)
{
	gchar *path_a, *path_b;
	gtk_tree_model_get(model, a, COL_THEME_FILE, &path_a, -1);
	gtk_tree_model_get(model, b, COL_THEME_FILE, &path_b, -1);
	gchar *name_a = path_a;
	gchar *p;
	for (p = name_a; *p; p++) {
		if (*p == '/')
			name_a = p + 1;
	}
	gchar *name_b = path_b;
	for (p = name_b; *p; p++) {
		if (*p == '/')
			name_b = p + 1;
	}
	if (g_str_equal(name_a, name_b))
		return 0;
	if (g_str_equal(name_a, "tint0rc"))
		return -1;
	if (g_str_equal(name_b, "tint0rc"))
		return 1;
	gint result = strnatcasecmp(name_a, name_b);
	g_free(path_a);
	g_free(path_b);
	return result;
}

void custom_list_append(const gchar *path)
{
	GtkTreeIter  iter;

	gtk_list_store_append(g_store, &iter);
	gtk_list_store_set(g_store, &iter, COL_THEME_FILE, path, -1);

	gchar *name = g_strdup(strrchr(path, '/') + 1);
	gtk_list_store_set(g_store, &iter, COL_THEME_NAME, name, -1);
}


gboolean update_snapshot()
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GdkPixbuf *icon;
	gboolean have_iter;

	gint pixWidth = 200, pixHeight = 30;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(g_theme_view));
	have_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (have_iter) {
		gtk_tree_model_get(model, &iter, COL_SNAPSHOT, &icon, -1);
		if (icon != NULL) {
			g_object_unref(icon);
		}

		// build panel's snapshot
		GdkPixbuf *pixbuf = NULL;
		gchar *name, *snap, *cmd;

		snap = g_build_filename(g_get_user_config_dir(), "tint0", "snap.jpg", NULL);
		g_remove(snap);

		gtk_tree_model_get(model, &iter, COL_THEME_FILE, &name, -1);
		cmd = g_strdup_printf("tint0 -c \'%s\' -s \'%s\'", name, snap);
		if (system(cmd) == 0) {
			// load
			pixbuf = gdk_pixbuf_new_from_file(snap, NULL);
			if (pixbuf == NULL) {
				printf("snapshot NULL : %s\n", cmd);
			}
		}
		g_free(snap);
		g_free(cmd);
		g_free(name);

		gint w, h;
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);
		pixWidth = w > pixWidth ? w : pixWidth;
		pixHeight = h > pixHeight ? h : pixHeight;

		gtk_list_store_set(g_store, &iter, COL_SNAPSHOT, pixbuf, -1);

		have_iter = gtk_tree_model_iter_next(model, &iter);
	}

	gtk_cell_renderer_set_fixed_size(g_renderer, pixWidth + 30, pixHeight + 30);

	return FALSE;
}



