/*
 * Copyright (c) 2014 Stefan Mark <mark at unserver dot de>
 * Copyright (c) 2011 Thomas Adam <thomas@xteddy.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <glib.h>

struct ContainerData_ {
	GtkWidget *socket;
	int pid;
	GtkTreeRowReference *row;

	gchar *restore_cmd;
} typedef ContainerData;

struct Tabster_ {
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *vbox;

    GtkTreeView *tabtree;
    GtkTreeStore *tabmodel;

	GList *socket_list;

	int fifofd;
    char fifobuf[1024];
} typedef Tabster;

enum directions {
   STEP_NEXT,
   STEP_PREV,
};

static void die(const char *errstr, ...);

static void setup_window();
static gboolean checkfifo(gpointer data);
static void parse_cmd();

static ContainerData *new_socket_for_plug();
static GtkTreeRowReference *new_tab_page(GtkWidget *socket, gboolean as_child);
static int spawn(gchar *cmd, int socket);
static void spawn_new_tab(gchar *cmd, gboolean in_background, gboolean as_child);

static ContainerData *get_cd_by_pid(gint pid);
static ContainerData *get_nth_cd(gint n);
static void set_page(gint i);
static void linear_step(int dir);
static gboolean get_iter_by_cd(ContainerData *cd, GtkTreeIter *iter);
static void set_pid_tab_title(gint pid, gchar *title);
static void set_pid_tab_restore(gint pid, gchar *restore);
static void close_nth(gint n);

static void page_removed_cb(GtkNotebook *, GtkWidget *, guint, gpointer);
static void row_clicked_cb(GtkTreeView *view, gpointer data);

static gint by_pid(gconstpointer data, gconstpointer pid);
static gint by_widget(gconstpointer data, gconstpointer pid);


#define XALLOC(target, type, size) if((target = calloc(sizeof(type), size)) == NULL) die("Error: calloc failed\n")

Tabster tabster;

void die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);

    exit(1);
}

void setup_window() {
	// ** create tree view
	tabster.tabtree = GTK_TREE_VIEW(gtk_tree_view_new()); // FREE
	// connect signals
    g_signal_connect(tabster.tabtree, "cursor-changed", G_CALLBACK(row_clicked_cb), NULL);
	// style
    gtk_widget_set_can_focus(GTK_WIDGET(tabster.tabtree), FALSE);
    gtk_tree_view_set_headers_visible(tabster.tabtree, FALSE);
    // * add tree store
    tabster.tabmodel = gtk_tree_store_new(1, G_TYPE_STRING); // FREE
    gtk_tree_view_set_model(tabster.tabtree, GTK_TREE_MODEL(tabster.tabmodel));
    // * add cell renderer
    GtkCellRenderer *trenderer = gtk_cell_renderer_text_new(); // FREE
    gtk_tree_view_insert_column_with_attributes(tabster.tabtree, -1, "", trenderer, "text", 0, NULL);

    // ** create notebook
	tabster.notebook = gtk_notebook_new(); // FREE
	// connect signals
	g_signal_connect(tabster.notebook, "page-removed", G_CALLBACK(page_removed_cb), (gpointer)&tabster);
	// style
	gtk_notebook_popup_enable(GTK_NOTEBOOK(tabster.notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabster.notebook), TRUE);
	gtk_notebook_set_tab_border(GTK_NOTEBOOK(tabster.notebook), 1);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK(tabster.notebook), GTK_POS_LEFT);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), TRUE); // TODO FALSE

	// ** create pane
    GtkPaned *pane = GTK_PANED(gtk_hpaned_new()); // FREE
    // style
    gtk_paned_set_position(pane, 200);
    // add widgets
    gtk_paned_add1(pane, GTK_WIDGET(tabster.tabtree));
    gtk_paned_add2(pane, GTK_WIDGET(tabster.notebook));

	// ** create window
	tabster.window = gtk_window_new(GTK_WINDOW_TOPLEVEL); // FREE
	// connect signals
	g_signal_connect(tabster.window, "delete-event", gtk_main_quit, NULL);
	// style
	gtk_window_set_default_size(GTK_WINDOW(tabster.window), 800, 600);
	// add widgets
	gtk_container_add(GTK_CONTAINER(tabster.window), GTK_WIDGET(pane));
	
	gtk_widget_show_all(tabster.window);
}

gboolean checkfifo(gpointer data) {
    int r, c = 0;

    // read from fifo
    for(;;) {
    	r = read(tabster.fifofd, tabster.fifobuf + c, 1);
    	if(!r || tabster.fifobuf[c]=='\n' || c>1022)
    		break;
    	c++;
    }

    // do something
    if(c) {
		// terminate command
		tabster.fifobuf[c] = '\0';
    	parse_cmd();
    }

    return TRUE;
}

void parse_cmd() {
	gint n;
	gchar** cmd;
    gchar** parts;

    // split command line at first space symbol
    cmd = g_strsplit(tabster.fifobuf, " ", 2); // FREED
	g_strstrip(cmd[0]);
    if(cmd[1]) g_strstrip(cmd[1]);

    printf("-%s-\n", tabster.fifobuf);
	    
    // new tab
    if(!g_strcmp0(cmd[0], "new"))
    	spawn_new_tab(cmd[1], FALSE, FALSE);
    if(!g_strcmp0(cmd[0], "cnew"))
    	spawn_new_tab(cmd[1], FALSE, TRUE);
    if(!g_strcmp0(cmd[0], "bnew"))
    	spawn_new_tab(cmd[1], TRUE, FALSE);
    if(!g_strcmp0(cmd[0], "bcnew"))
    	spawn_new_tab(cmd[1], TRUE, TRUE);

    // set tab attributes
    if(!g_strcmp0(cmd[0], "tabtitle")) {
        parts = g_strsplit(cmd[1], " ", 2); // FREED
        set_pid_tab_title(atoi(parts[0]), parts[1]);
		g_strfreev(parts);
    }
    if(!g_strcmp0(cmd[0], "restore_cmd")) {
        parts = g_strsplit(cmd[1], " ", 2); // FREED
        set_pid_tab_restore(atoi(parts[0]), parts[1]);
		g_strfreev(parts);	        
    }

    // tab selection
    if(!g_strcmp0(cmd[0], "prev"))
    	linear_step(STEP_PREV);
    if(!g_strcmp0(cmd[0], "next"))
    	linear_step(STEP_NEXT);
    if(!g_strcmp0(cmd[0], "goto")) {
    	n = atoi(cmd[1]);
    	set_page(n);
    }

    // close
    if(!g_strcmp0(cmd[0], "close")) {
    	n = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
    	close_nth(n);
    }
    if(!g_strcmp0(cmd[0], "treeclose")) {
    	// TODO
    	// currently, "close" is treeclose. i dont know if i want to implement a item-remove-rebuild-tree close-like feature
    }

    // tree manipulation
    if(!g_strcmp0(cmd[0], "move")) {
    	// TODO
    	// gint p = atoi(cmd[1]);
    	// gtk_notebook_reorder_child(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook))), p);
    }
    if(!g_strcmp0(cmd[0], "attach")) {	
    	// TODO
    }

    // interface stuff
    if(!g_strcmp0(cmd[0], "hidetree")) {
    	// TODO
    	// gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), FALSE);
    }
    if(!g_strcmp0(cmd[0], "showtree")) {
    	// TODO
    	// gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), TRUE);
    }

	g_strfreev(cmd);
	*tabster.fifobuf = '\0';
}

ContainerData *new_socket_for_plug() {
	ContainerData *cd;

	XALLOC(cd, ContainerData, 1); // FREED
	cd->socket = gtk_socket_new(); // FREE / CLOSE

	return cd;
}

GtkTreeRowReference *new_tab_page(GtkWidget *socket, gboolean as_child) {
    GtkTreeIter iter, *piter;
	GtkTreePath *p;

	// display plug
	gtk_widget_show(socket);
	gtk_notebook_append_page(GTK_NOTEBOOK(tabster.notebook), socket, NULL);

	// get gtk_tree_path of current tab (or create empty)
    gint parent_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
    GList *l = g_list_nth(tabster.socket_list, parent_num);
	if(l) {
    	p = gtk_tree_row_reference_get_path(((ContainerData*)l->data)->row); //FREED
    } else {
    	p = gtk_tree_path_new_from_indices(0, -1); //FREED
    }

    if(!as_child) {
    	// get parent for new tab
    	if(gtk_tree_path_get_depth(p)>1) {
    		gtk_tree_path_up(p);
    		XALLOC(piter, GtkTreeIter, 1); // FREED
	        gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), piter, p);
    	} else {
    		piter = NULL;
    	}
	} else {
		XALLOC(piter, GtkTreeIter, 1); // FREED
        gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), piter, p);    	
    }
    // append new row
    gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &iter, piter);
    if(as_child)
    	gtk_tree_view_expand_row(tabster.tabtree, p, FALSE);

	gtk_tree_path_free(p);
    p = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &iter);

    GtkTreeRowReference *ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(tabster.tabmodel), p);
	gtk_tree_path_free(p);
	free(piter);

    return ref;
}

int spawn(gchar *cmd, int socket) {
	gchar *xcmd = g_strdup_printf(cmd, socket); // FREED
    gint argc;
    gchar** argv = NULL;
    int pid;

    g_shell_parse_argv(xcmd, &argc, &argv, NULL);
    GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH );//TODO | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL
    g_spawn_async(NULL, argv, NULL, flags, NULL, NULL, &pid, NULL);

    g_free(xcmd);
    g_strfreev(argv); // TODO: i guess thats not needed

    return pid;
}

void spawn_new_tab(gchar *cmd, gboolean in_background, gboolean as_child) {
	ContainerData *cd;

	cd = new_socket_for_plug();	    	
	cd->row = new_tab_page(GTK_WIDGET(cd->socket), as_child); // FREED
	cd->pid = spawn(cmd, gtk_socket_get_id(GTK_SOCKET(cd->socket))); // FREED
	cd->restore_cmd = g_strdup(cmd); // FREED

	tabster.socket_list = g_list_append(tabster.socket_list, cd); // FREED
	if(!in_background)
		set_page(g_list_length(tabster.socket_list) - 1);
}

ContainerData *get_cd_by_pid(gint pid) {
    GList *l;

    l = g_list_find_custom(tabster.socket_list, &pid, by_pid);
	if(l) {
		return (ContainerData*)l->data;
	}
	return NULL;
}

ContainerData *get_nth_cd(gint n) {
	GList *l;

	l = g_list_nth(tabster.socket_list, n);
	if(l) {
		return (ContainerData*)l->data;
	}
	return NULL;
}

void set_page(gint n) {
    GtkTreeIter iter;
    ContainerData *cd;

    // select tab in notebook
	gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), n);

	// select row in tree
    cd = get_nth_cd(n);
    if(cd) {
    	get_iter_by_cd(cd, &iter);
    	GtkTreeSelection *sel = gtk_tree_view_get_selection(tabster.tabtree); // NO FREE NEEDED
    	gtk_tree_selection_select_iter(sel, &iter);
    }
}

void linear_step(int dir) {
    GtkTreeIter iter;
	ContainerData *cd;
	GtkTreePath *path, *p = NULL;
	GList *l;

	cd = get_nth_cd(gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook)));
	if(cd) {
		path = gtk_tree_row_reference_get_path(cd->row); // FREED
		gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path);

		if(dir==STEP_NEXT) { // ** next
			if(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(tabster.tabmodel), &iter)) {
				gtk_tree_path_down(path);
			} else {
				gtk_tree_path_next(path);
				while(!gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path) && gtk_tree_path_get_depth(path)>1 && gtk_tree_path_up(path))
					gtk_tree_path_next(path);
				if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path)) {
					gtk_tree_path_free(path);
					path = gtk_tree_path_new_from_indices(0, -1);
				}
			}
		} else if(dir==STEP_PREV) { // ** prev
			if(gtk_tree_path_prev(path)) {
				gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path);
				while(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(tabster.tabmodel), &iter)) {
					gint lc = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), &iter) - 1;
					gtk_tree_path_append_index(path, lc);
					gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path);
				}
			} else {
				if(gtk_tree_path_get_depth(path)>1) {
					gtk_tree_path_up(path);
				} else {
					gint lc = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), NULL) - 1;
					gtk_tree_path_free(path);
					path = gtk_tree_path_new_from_indices(lc, -1);					
				}
			}
		}

	    int i = 0;
	    for(l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
	        p = gtk_tree_row_reference_get_path(((ContainerData*)l->data)->row); // FREED
	        if(!gtk_tree_path_compare(path, p)) {
	            set_page(i);
	            break;
	        }
	        i++;
			gtk_tree_path_free(p);
	    }
		gtk_tree_path_free(path);
	}
}

gboolean get_iter_by_cd(ContainerData *cd, GtkTreeIter *iter) {
    gboolean b;
    GtkTreeRowReference *r;
    GtkTreePath *p;

    r = cd->row;
	p = gtk_tree_row_reference_get_path(r);
	b = gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), iter, p);
	gtk_tree_path_free(p);    	
	return b;
}

void set_pid_tab_title(gint pid, gchar *title) {
    ContainerData *cd;
    GtkTreeIter iter;

    cd = get_cd_by_pid(pid);
    if(cd) {
		get_iter_by_cd(cd, &iter);
		gtk_tree_store_set(GTK_TREE_STORE(tabster.tabmodel), &iter, 0, title, -1);
    }
}

void set_pid_tab_restore(gint pid, gchar *restore) {
    ContainerData *cd;

    cd = get_cd_by_pid(pid);
    if(cd) {
		g_free(cd->restore_cmd);
		cd->restore_cmd = g_strdup(restore);
    }
}

void close_nth(gint n) {
	linear_step(STEP_PREV);
    gtk_notebook_remove_page(GTK_NOTEBOOK(tabster.notebook), n);
}

void page_removed_cb(GtkNotebook *nb, GtkWidget *widget, guint id, gpointer data) {
	gboolean has_child;
	gint i;
	GtkTreeIter iter, citer;
	GList *l;
	GtkTreePath *path, *p;
	ContainerData *cd, *cd2;

	l = g_list_find_custom(tabster.socket_list, widget, by_widget);
	if(l) {
		cd = (ContainerData*)l->data;

		// close pid
		g_spawn_close_pid(cd->pid);
		// free restore_cmd
		g_free(cd->restore_cmd);
		// remove element from list
		tabster.socket_list = g_list_remove(tabster.socket_list, l->data);

		// test for child nodes
	    get_iter_by_cd(cd, &iter);
	    has_child = gtk_tree_model_iter_has_child(GTK_TREE_MODEL(tabster.tabmodel), &iter);
	    // remove child nodes
	    if(has_child) {
	    	// one by one
	    	i = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), &iter) - 1;
	    	for(;i>=0;i--) {
	    		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(tabster.tabmodel), &citer, &iter, i);
				path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &citer);
			    // find cd by path
			    for(l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
			        cd2 = (ContainerData*)l->data;
			        p = gtk_tree_row_reference_get_path(cd2->row); // FREED
			        if(!gtk_tree_path_compare(path, p)) {
						// remove page (heere is the iteration!)
						gtk_notebook_remove_page(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_page_num(GTK_NOTEBOOK(tabster.notebook), GTK_WIDGET(cd2->socket)));
			            break;
			        }
					gtk_tree_path_free(p);
			    }
				gtk_tree_path_free(path);
	    	}
        }

	    // remove tree
	    get_iter_by_cd(cd, &iter);
		gtk_tree_store_remove(GTK_TREE_STORE(tabster.tabmodel), &iter);
		// free ContainerData
		g_free(cd);

		// quit if list is empty
		if(!g_list_length(tabster.socket_list)) {
			g_list_free(tabster.socket_list);
			gtk_main_quit();
		}

	}
}

void row_clicked_cb(GtkTreeView *view, gpointer data) {
    GtkTreeIter iter;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tabster.tabtree);
    gtk_tree_selection_get_selected(sel, NULL, &iter);
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &iter);

    int i = 0;
    for(GList *l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
        ContainerData *cd = (ContainerData*)l->data;

        GtkTreeRowReference *r = cd->row;
        GtkTreePath *p = gtk_tree_row_reference_get_path(r);

        if(!gtk_tree_path_compare(path, p)) {
            set_page(i);
            break;
        }
        i++;
    }
}

gint by_pid(gconstpointer data, gconstpointer pid) {
	return ((ContainerData*)data)->pid==*(int*)pid ? 0 : 1;
}
gint by_widget(gconstpointer data, gconstpointer widget) {
	return ((ContainerData*)data)->socket==(GtkWidget*)widget ? 0 : 1;
}

int main(int argc, char **argv) {
	gboolean version = FALSE;
	GError *error = NULL;
	int pid = getpid();
	gchar *fdfn = g_strdup_printf("/tmp/tabster%d", pid);
	gchar *env_pid = g_strdup_printf("TABSTER_PID=%d", pid);
	putenv(env_pid);

	GOptionEntry cmdline_ops[] = {
		{
			"version",
			'v',
			0,
			G_OPTION_ARG_NONE,
			&version,
			"Print version information",
			NULL
		},
		{ NULL }
	};

	if(!gtk_init_with_args( &argc, &argv, "foo", cmdline_ops, NULL, &(error))) {
		g_printerr( "Can't init gtk: %s\n", error->message );
		g_error_free( error );
		return EXIT_FAILURE;
	}

	if (version) {
		fprintf(stderr, "%s %s\n", "tabster ", VERSION);
		return EXIT_SUCCESS;
	}

	setup_window();

    mkfifo(fdfn, 0766);
    tabster.fifofd = open(fdfn, O_NONBLOCK);

    g_timeout_add(100, checkfifo, NULL);

	gtk_main();

    close(tabster.fifofd);
    unlink(fdfn);
    g_free(fdfn);
	g_free(env_pid);

	return EXIT_SUCCESS;
}
