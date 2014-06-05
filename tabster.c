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
	gchar *title;
} typedef ContainerData;

struct Tabster_ {
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *vbox;
	GtkPaned *pane;

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
static GtkTreeRowReference *new_tab_page(GtkWidget *socket, GtkTreeIter *parent);
static int spawn(gchar *cmd, int socket);
static void spawn_new_tab(gchar *cmd, gboolean in_background, gboolean as_child);

static ContainerData *get_cd_by(gconstpointer data, GCompareFunc func);
static ContainerData *get_cd_by_iter(GtkTreeIter *iter);
static void set_page(gint i);
static gint linear_step(int dir, gint page, gboolean turn_around);
static gboolean get_iter_by_cd(ContainerData *cd, GtkTreeIter *iter);
static void set_pid_tab_title(gint pid, gchar *title);
static void set_pid_tab_restore(gint pid, gchar *restore);
static void close_nth(gint n);

static void page_removed_cb(GtkNotebook *, GtkWidget *, guint, gpointer);
static void row_clicked_cb(GtkTreeView *view, gpointer data);

static gint by_pid(gconstpointer data, gconstpointer pid);
static gint by_widget(gconstpointer data, gconstpointer pid);
static gint by_path(gconstpointer data, gconstpointer path);
static void save_session();


#define XALLOC(target, type, size) if((target = calloc(sizeof(type), size)) == NULL) die("Error: calloc failed\n")

#define CURPAGE gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook))
#define NTH_PAGE(n) gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabster.notebook), n), by_widget

Tabster tabster;
static gint tree_pane_width = 200;

void die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);

    exit(1);
}

void setup_window() {
	GtkCellRenderer *trenderer;

	// ** create tree view
	tabster.tabtree = GTK_TREE_VIEW(gtk_tree_view_new());
	// connect signals
    g_signal_connect(tabster.tabtree, "cursor-changed", G_CALLBACK(row_clicked_cb), NULL);
	// style
    gtk_widget_set_can_focus(GTK_WIDGET(tabster.tabtree), FALSE);
    gtk_tree_view_set_headers_visible(tabster.tabtree, FALSE);
    // * add tree store
    tabster.tabmodel = gtk_tree_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(tabster.tabtree, GTK_TREE_MODEL(tabster.tabmodel));
    // * add cell renderer
    trenderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(tabster.tabtree, -1, "", trenderer, "text", 0, NULL);

    // ** create notebook
	tabster.notebook = gtk_notebook_new();
	// connect signals
	g_signal_connect(tabster.notebook, "page-removed", G_CALLBACK(page_removed_cb), (gpointer)&tabster);
	// style
	gtk_notebook_popup_enable(GTK_NOTEBOOK(tabster.notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabster.notebook), TRUE);
	gtk_notebook_set_tab_border(GTK_NOTEBOOK(tabster.notebook), 1);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK(tabster.notebook), GTK_POS_LEFT);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), FALSE);

	// ** create pane
    tabster.pane = GTK_PANED(gtk_hpaned_new());
    // style
    gtk_paned_set_position(tabster.pane, tree_pane_width);
    // add widgets
    gtk_paned_add1(tabster.pane, GTK_WIDGET(tabster.tabtree));
    gtk_paned_add2(tabster.pane, GTK_WIDGET(tabster.notebook));

	// ** create window
	tabster.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	// connect signals
	g_signal_connect(tabster.window, "delete-event", gtk_main_quit, NULL);
	// style
	gtk_window_set_default_size(GTK_WINDOW(tabster.window), 800, 600);
	// add widgets
	gtk_container_add(GTK_CONTAINER(tabster.window), GTK_WIDGET(tabster.pane));
	
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
    cmd = g_strsplit(tabster.fifobuf, " ", 2); // FREE parse_cmd/cmd
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

    if(!g_strcmp0(cmd[0], "add")) {
        parts = g_strsplit(cmd[1], " ", 2); // FREE parse_cmd/parts

	    GtkTreePath *path;
	    GtkTreeIter piter;
	    ContainerData *cd;

	    path = gtk_tree_path_new_from_string(parts[0]); // FREE ?/path
		cd = new_socket_for_plug();
	    if(gtk_tree_path_get_depth(path)>1) {
	    	gtk_tree_path_up(path);
		   	gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &piter, path);
			cd->row = new_tab_page(GTK_WIDGET(cd->socket), &piter);
	    } else {
			cd->row = new_tab_page(GTK_WIDGET(cd->socket), NULL);
	    }
	    gtk_tree_path_free(path); // FREED ?/path
		cd->pid = spawn(parts[1], gtk_socket_get_id(GTK_SOCKET(cd->socket)));
		cd->restore_cmd = g_strdup(parts[1]); // FREE /cd->restore_cmd
		tabster.socket_list = g_list_append(tabster.socket_list, cd); // FREE /tabster.socket_list[]

		g_strfreev(parts); // FREED parse_cmd/parts
    }

    // set tab attributes
    if(!g_strcmp0(cmd[0], "tabtitle")) {
        parts = g_strsplit(cmd[1], " ", 2); // FREE parse_cmd/parts
        set_pid_tab_title(atoi(parts[0]), parts[1]);
		g_strfreev(parts); // FREED parse_cmd/parts
    }
    if(!g_strcmp0(cmd[0], "restore_cmd")) {
        parts = g_strsplit(cmd[1], " ", 2); // FREE parse_cmd/parts
        set_pid_tab_restore(atoi(parts[0]), parts[1]);
		g_strfreev(parts); // FREED parse_cmd/parts
    }

    // tab selection
    if(!g_strcmp0(cmd[0], "prev"))
    	set_page(linear_step(STEP_PREV, CURPAGE, TRUE));
    if(!g_strcmp0(cmd[0], "next"))
    	set_page(linear_step(STEP_NEXT, CURPAGE, TRUE));
    if(!g_strcmp0(cmd[0], "goto")) {
    	n = atoi(cmd[1]);
    	set_page(n);
    }

    // close
    if(!g_strcmp0(cmd[0], "close")) {
    	close_nth(CURPAGE);
    }
    if(!g_strcmp0(cmd[0], "treeclose")) {
    	// TODO
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
    	gtk_paned_set_position(tabster.pane, 0);
    }
    if(!g_strcmp0(cmd[0], "showtree")) {
    	gtk_paned_set_position(tabster.pane, tree_pane_width);
    }

	g_strfreev(cmd); // FREED parse_cmd/cmd
	*tabster.fifobuf = '\0';
}

ContainerData *new_socket_for_plug() {
	ContainerData *cd;

	XALLOC(cd, ContainerData, 1); // FREE /cd
	cd->socket = gtk_socket_new(); // FREE /cd->socket

	return cd;
}

GtkTreeRowReference *new_tab_page(GtkWidget *socket, GtkTreeIter *piter) {
    GtkTreeIter iter;//, *piter;
	GtkTreePath *p;

	gtk_widget_show(socket);
	gtk_notebook_append_page(GTK_NOTEBOOK(tabster.notebook), socket, NULL);

    // append new row
    gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &iter, piter);

    if(piter) {
	    p = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), piter); // FREE new_tab_page/p
    	gtk_tree_view_expand_row(tabster.tabtree, p, FALSE);
		gtk_tree_path_free(p); // FREED new_tab_page/p
    }

    p = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &iter); // FREE new_tab_page/p

    GtkTreeRowReference *ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(tabster.tabmodel), p); // FREE /ref
	gtk_tree_path_free(p); // FREED new_tab_page/p

    return ref;
}

int spawn(gchar *cmd, int socket) {
	gchar *xcmd = g_strdup_printf(cmd, socket); // FREE spwan/xcmd
    gint argc;
    gchar** argv = NULL;
    int pid;

    g_shell_parse_argv(xcmd, &argc, &argv, NULL); // TODO does this need to be freed
    GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH );//TODO | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL
    g_spawn_async(NULL, argv, NULL, flags, NULL, NULL, &pid, NULL);

    g_free(xcmd); // FREED spwan/xcmd
    g_strfreev(argv); // TODO: i guess thats not needed

    return pid;
}

void spawn_new_tab(gchar *cmd, gboolean in_background, gboolean as_child) {
	ContainerData *new_cd, *cur_cd;
	GtkTreePath *p;
	GtkTreeIter *piter;

	cur_cd = get_cd_by(NTH_PAGE(CURPAGE));
	if(cur_cd)
		p = gtk_tree_row_reference_get_path(cur_cd->row); // FREE spawn_new_tab/p
	else
		p = gtk_tree_path_new_from_indices(0, -1); // ...

    if(!as_child) {
    	// get parent for new tab
    	if(gtk_tree_path_get_depth(p)>1) {
    		gtk_tree_path_up(p);
    		XALLOC(piter, GtkTreeIter, 1); // FREE spawn_new_tab/piter
	        gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), piter, p);
    	} else {
    		piter = NULL;
    	}
	} else {
		XALLOC(piter, GtkTreeIter, 1); // ...
        gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), piter, p);    	
    }
    gtk_tree_path_free(p); // FREED spawn_new_tab/p

	new_cd = new_socket_for_plug();	    	
	new_cd->row = new_tab_page(GTK_WIDGET(new_cd->socket), piter); // FREE /new_cd->row
	new_cd->pid = spawn(cmd, gtk_socket_get_id(GTK_SOCKET(new_cd->socket))); // FREE /new_cd->pid
	new_cd->restore_cmd = g_strdup(cmd); // FREE /new_cd->restore_cmd

    free(piter); // FREED spawn_new_tab/piter

	tabster.socket_list = g_list_append(tabster.socket_list, new_cd); // FREE /tabster.socket_list[]
	if(!in_background)
		set_page(g_list_length(tabster.socket_list) - 1);

	save_session();
}

ContainerData *get_cd_by(gconstpointer data, GCompareFunc func) {
    GList *l;

    l = g_list_find_custom(tabster.socket_list, data, func);
	return l ? (ContainerData*)l->data : NULL;
}

ContainerData *get_cd_by_iter(GtkTreeIter *iter) {
    GtkTreePath *path;
    ContainerData *cd;

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), iter); // FREE get_cd_by_iter/path
	cd = get_cd_by(path, by_path);
	gtk_tree_path_free(path); // FREED get_cd_by_iter/path
	return cd;
}

void set_page(gint n) {
    GtkTreeIter iter;
    ContainerData *cd;

    if(n<0)
    	return;

    // select tab in notebook
	gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), n);

	// select row in tree
    cd = get_cd_by(NTH_PAGE(n));
    if(cd) {
    	get_iter_by_cd(cd, &iter);
    	GtkTreeSelection *sel = gtk_tree_view_get_selection(tabster.tabtree); // NO FREE NEEDED
    	gtk_tree_selection_select_iter(sel, &iter);
    }
}

gint linear_step(int dir, gint page, gboolean turn_around) {
    GtkTreeIter iter;
	ContainerData *cd;
	GtkTreePath *path;

	cd = get_cd_by(NTH_PAGE(page));
	if(cd) {
		path = gtk_tree_row_reference_get_path(cd->row); // FREE linear_step/path
		gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path);

		if(dir==STEP_NEXT) { // ** next
			if(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(tabster.tabmodel), &iter)) {
				gtk_tree_path_down(path);
			} else {
				gtk_tree_path_next(path);
				while(!gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path) && gtk_tree_path_get_depth(path)>1 && gtk_tree_path_up(path))
					gtk_tree_path_next(path);
				if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, path)) {
					gtk_tree_path_free(path); // FREED linear_step/path
					if(turn_around)
						path = gtk_tree_path_new_from_indices(0, -1); // FREE linear_step/path
					else
						return -1;
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
				} else if(turn_around) {
					gint lc = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), NULL) - 1;
					gtk_tree_path_free(path); // FREED linear_step/path
					path = gtk_tree_path_new_from_indices(lc, -1); // FREE linear_step/path
				} else {
					gtk_tree_path_free(path);
					return -1;
				}
			}
		}

	    cd = get_cd_by(path, by_path);
	    if(cd)
	    	return gtk_notebook_page_num(GTK_NOTEBOOK(tabster.notebook), cd->socket);
		gtk_tree_path_free(path); // FREED linear_step/path
	}
	return -1;
}

gboolean get_iter_by_cd(ContainerData *cd, GtkTreeIter *iter) {
    gboolean b;
    GtkTreePath *p;

	p = gtk_tree_row_reference_get_path(cd->row); // FREE get_iter_by_cd/p
	b = gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), iter, p);
	gtk_tree_path_free(p); // FREED get_iter_by_cd/p
	return b;
}

void set_pid_tab_title(gint pid, gchar *title) {
    ContainerData *cd;
    GtkTreeIter iter;

    cd = get_cd_by(&pid, by_pid);
    if(cd) {
		get_iter_by_cd(cd, &iter);
		gtk_tree_store_set(GTK_TREE_STORE(tabster.tabmodel), &iter, 0, title, -1);
		cd->title = g_strdup(title); // FREE /cd->title
    }
}

void set_pid_tab_restore(gint pid, gchar *restore) {
    ContainerData *cd;
    cd = get_cd_by(&pid, by_pid);

    if(cd) {
		g_free(cd->restore_cmd);
		cd->restore_cmd = g_strdup(restore); // FREE /cd->restore_cmd

		save_session();
    }
}

void close_nth(gint n) {
	set_page(linear_step(STEP_PREV, CURPAGE, TRUE));
    gtk_notebook_remove_page(GTK_NOTEBOOK(tabster.notebook), n);
}

void remove_row(GtkTreeIter *iter, GtkTreeIter *piter) {
    int i;
    ContainerData *cd;
    GtkTreePath *path;//, *p, *npath;
    GtkTreeIter citer, niter;

    if(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(tabster.tabmodel), iter)) {
    	// one by one
    	i = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), iter) - 1;
    	for(;i>=0;i--) {
    		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(tabster.tabmodel), &citer, iter, i);
			cd = get_cd_by_iter(&citer);
			if(cd) {
			    gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &niter, piter);
				gtk_tree_store_set(GTK_TREE_STORE(tabster.tabmodel), &niter, 0, cd->title, -1);
	        	remove_row(&citer, &niter);
				path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &niter); // FREE remove_row/path
			    cd->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(tabster.tabmodel), path);
			    gtk_tree_path_free(path); // FREED remove_row/path
			}
    	}
    	if(piter!=NULL) {
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), piter); // FREE remove_row/path
    		gtk_tree_view_expand_row(tabster.tabtree, path, FALSE);
			gtk_tree_path_free(path); // FREED remove_row/path
    	}
    }
	gtk_tree_store_remove(GTK_TREE_STORE(tabster.tabmodel), iter);
}

void page_removed_cb(GtkNotebook *nb, GtkWidget *widget, guint id, gpointer data) {
	GtkTreeIter iter, piter;
	GList *l;
	ContainerData *cd;

	l = g_list_find_custom(tabster.socket_list, widget, by_widget);
	if(l) {
		cd = (ContainerData*)l->data;

		g_spawn_close_pid(cd->pid); // FREED /cd->pid
		g_free(cd->restore_cmd); // FREED /cd->resore_cmd
		g_free(cd->title); // FREED /cd->title
		tabster.socket_list = g_list_remove(tabster.socket_list, l->data); // FREED /tabster.socket_list[]

	    // remove row from tree
	    get_iter_by_cd(cd, &iter);	    
	    if(gtk_tree_model_iter_parent(GTK_TREE_MODEL(tabster.tabmodel), &piter, &iter))
	    	remove_row(&iter, &piter);
	    else
	    	remove_row(&iter, NULL);

		// FREED /cd
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
    GtkTreeSelection *sel;
    GtkTreePath *path;//, *p;
    ContainerData *cd;

    sel = gtk_tree_view_get_selection(tabster.tabtree);
    gtk_tree_selection_get_selected(sel, NULL, &iter);
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &iter); // FREE row_clicked_cb/path

    cd = get_cd_by(path, by_path);
    if(cd)
    	set_page(gtk_notebook_page_num(GTK_NOTEBOOK(tabster.notebook), cd->socket));

    gtk_tree_path_free(path); // FREE row_clicked_cb/path
}

gint by_pid(gconstpointer data, gconstpointer pid) {
	return ((ContainerData*)data)->pid==*(int*)pid ? 0 : 1;
}
gint by_widget(gconstpointer data, gconstpointer widget) {
	return ((ContainerData*)data)->socket==(GtkWidget*)widget ? 0 : 1;
}
gint by_path(gconstpointer data, gconstpointer path) {
	gint r;
	GtkTreePath *p;

	p = gtk_tree_row_reference_get_path(((ContainerData*)data)->row); // FREE_by_path_p
	r = !gtk_tree_path_compare(path, p) ? 0 : 1;
    gtk_tree_path_free(p); // FREED_by_path_p
    return r;
}

void save_session() {
	int sessionfd;
	char *s, *sessionfn;
	gchar *paths;
	gint page = 0;
	ContainerData *cd = NULL;
	GtkTreePath *path;

	s = getenv("XDG_DATA_HOME");
	if(s)
		sessionfn = g_strdup_printf("%s/uzbl/tabster.sess", s);
	else {
		s = getenv("HOME");
		if(s)
			sessionfn = g_strdup_printf("%s/.local/share/uzbl/tabster.sess", s);
		else
			die("$HOME not set");
	}

	sessionfd = open(sessionfn, O_WRONLY|O_CREAT|O_TRUNC);
    fchmod(sessionfd, 0666);

	for(page = 0; page>=0; page=linear_step(STEP_NEXT, page, FALSE)) {
		cd = get_cd_by(NTH_PAGE(page));
		if(cd) {
			path = gtk_tree_row_reference_get_path(cd->row);
			paths = gtk_tree_path_to_string(path);
			write(sessionfd, "add ", 4);
			write(sessionfd, paths, strlen(paths));
			g_free(paths);
			write(sessionfd, " ", 1);
			write(sessionfd, cd->restore_cmd, strlen(cd->restore_cmd));
			write(sessionfd, "\n", 1);
			gtk_tree_path_free(path);
		}
	}

    close(sessionfd);
}

int main(int argc, char **argv) {
	gboolean version = FALSE;
	int pid;
	gchar *fdfn;
	gchar *env_pid;
	GError *error = NULL;

	pid = getpid();
	fdfn = g_strdup_printf("/tmp/tabster%d", pid); // FREE main/fdfn
	env_pid = g_strdup_printf("TABSTER_PID=%d", pid); // FREE main/env_pid
	putenv(env_pid);

	GOptionEntry cmdline_ops[] = { {
		"version",
		'v',
		0,
		G_OPTION_ARG_NONE,
		&version,
		"Print version information",
		NULL
	}, {
		NULL
	} };

	if(!gtk_init_with_args(&argc, &argv, "foo", cmdline_ops, NULL, &(error))) {
		g_printerr("Can't init gtk: %s\n", error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	if(version) {
		fprintf(stderr, "%s %s\n", "tabster ", VERSION);
		return EXIT_SUCCESS;
	}

	setup_window();

    mkfifo(fdfn, 0766); // FREE main/fifo
    tabster.fifofd = open(fdfn, O_NONBLOCK); // FREE main/tabster.fifofd

    // load_session();

    g_timeout_add(100, checkfifo, NULL);

	gtk_main();

    close(tabster.fifofd); // FREED main/tabster.fifofd
    unlink(fdfn); // FREED main/fifo
    g_free(fdfn); // FREED main/fdfn
	g_free(env_pid); // FREED main/env_pid

	return EXIT_SUCCESS;
}

// void page_removed_cb(GtkNotebook *nb, GtkWidget *widget, guint id, gpointer data) {
// 	gboolean has_child;
// 	gint i;
// 	GtkTreeIter iter, citer;
// 	GList *l;
// 	GtkTreePath *path, *p;
// 	ContainerData *cd, *cd2;

// 	l = g_list_find_custom(tabster.socket_list, widget, by_widget);
// 	if(l) {
// 		cd = (ContainerData*)l->data;

// 		// close pid
// 		g_spawn_close_pid(cd->pid);
// 		// free restore_cmd
// 		g_free(cd->restore_cmd);
// 		// remove element from list
// 		tabster.socket_list = g_list_remove(tabster.socket_list, l->data);

// 		// test for child nodes
// 	    get_iter_by_cd(cd, &iter);
// 	    has_child = gtk_tree_model_iter_has_child(GTK_TREE_MODEL(tabster.tabmodel), &iter);
// 	    // remove child nodes
// 	    if(has_child) {
// 	    	// one by one
// 	    	i = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), &iter) - 1;
// 	    	for(;i>=0;i--) {
// 	    		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(tabster.tabmodel), &citer, &iter, i);
// 				path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &citer);
// 			    // find cd by path
// 			    for(l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
// 			        cd2 = (ContainerData*)l->data;
// 			        p = gtk_tree_row_reference_get_path(cd2->row); // FREED
// 			        if(!gtk_tree_path_compare(path, p)) {
// 						// remove page (heere is the iteration!)
// 						gtk_notebook_remove_page(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_page_num(GTK_NOTEBOOK(tabster.notebook), GTK_WIDGET(cd2->socket)));
// 			            break;
// 			        }
// 					gtk_tree_path_free(p);
// 			    }
// 				gtk_tree_path_free(path);
// 	    	}
//         }

// 	    // remove tree
// 	    get_iter_by_cd(cd, &iter);
// 		gtk_tree_store_remove(GTK_TREE_STORE(tabster.tabmodel), &iter);
//      // free row reference
//      gtk_tree_row_reference_free(cd->row);
// 		// free ContainerData
// 		g_free(cd);

// 		// quit if list is empty
// 		if(!g_list_length(tabster.socket_list)) {
// 			g_list_free(tabster.socket_list);
// 			gtk_main_quit();
// 		}

// 	}
// }







			// path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &citer); // FREED
			// // find cd by path
			// for(l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
			// 	cd = (ContainerData*)l->data;
			// 	p = gtk_tree_row_reference_get_path(cd->row); // FREED
			// 	if(!gtk_tree_path_compare(path, p)) {
			// 		gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &niter, piter);
			// 		gtk_tree_store_set(GTK_TREE_STORE(tabster.tabmodel), &niter, 0, cd->title, -1);
			// 		remove_row(&citer, &niter);
			// 		npath = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &niter); // FREED
			// 		cd->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(tabster.tabmodel), npath);
			// 		gtk_tree_path_free(npath);
			// 		// TODO: aufklappen!
			// 		break;
			// 	}
			// 	gtk_tree_path_free(p);
			// }
			// gtk_tree_path_free(path);


// ContainerData *get_cd_by_pid(gint pid) {
//     GList *l;

//     l = g_list_find_custom(tabster.socket_list, &pid, by_pid);
// 	return l ? (ContainerData*)l->data : NULL;
// }
// ContainerData *get_cd_by_page(gint page) {
//     GList *l;
//     GtkWidget *widget;

//     widget = gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabster.notebook), page);
//     l = g_list_find_custom(tabster.socket_list, widget, by_widget);
// 	return l ? (ContainerData*)l->data : NULL;
// }
