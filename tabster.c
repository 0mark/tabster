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
	int id;
	GtkWidget* socket;
	int pid;
	gchar* uri;
	GtkTreeRowReference* row;
} typedef ContainerData;

struct Tabster_ {
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *vbox;

    GtkTreeView* tabtree;
    GtkTreeStore* tabmodel;


	GList *socket_list;

	int fifofd;
    char fifobuf[1024];
} typedef Tabster;

enum directions {
   STEP_NEXT,
   STEP_PREV,
};

static void die(const char *errstr, ...);
static void page_removed_cb(GtkNotebook *, GtkWidget *, guint, gpointer);
static GtkTreeRowReference* new_tab_page(GtkWidget *, gboolean bg, gboolean as_child);
static ContainerData* setup_new_socket_for_plug();
static int spawn(gchar* cmd, int socket);
static gint by_pid(gconstpointer data, gconstpointer pid);
static gint by_widget(gconstpointer data, gconstpointer pid);
static void setup_window();
static void set_page(gint i);
static void linear_step(int dir);
static gboolean checkfifo(gpointer data);

#define XALLOC(target, type, size) if((target = calloc(sizeof(type), size)) == NULL) die("Error: calloc failed\n")

Tabster tabster;

void die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);

    exit(1);
}

void page_removed_cb(GtkNotebook *nb, GtkWidget *widget, guint id, gpointer data) {
	GList* l = g_list_find_custom(tabster.socket_list, widget, by_widget);
	ContainerData* cd = (ContainerData*)l->data;
	g_spawn_close_pid(cd->pid);
	g_free(cd->uri);
	g_free(cd);
	tabster.socket_list = g_list_remove(tabster.socket_list, l->data);
	if(!g_list_length(tabster.socket_list)) {
		g_list_free(tabster.socket_list);
		gtk_main_quit();
	}
}

GtkTreeRowReference* new_tab_page(GtkWidget *socket, gboolean bg, gboolean as_child) {
	gtk_widget_show(socket);
	gtk_notebook_append_page(GTK_NOTEBOOK(tabster.notebook), socket, NULL);
	if(bg!=TRUE)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), -1);
	// gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(tabster.notebook), socket, TRUE);
    // gtk_widget_set_can_focus(GTK_WIDGET(tabster.notebook), FALSE);


    // UzblInstance* uzin = new UzblInstance(url, notebook, ref);
    // uzin->SetPNum(-1);
    // uzin->SetParent(NULL);

    GtkTreeIter iter;
	GtkTreePath* p;

    gint parent_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
    GList* l = g_list_nth(tabster.socket_list, parent_num);
	if(l) {
    	GtkTreeRowReference* r = ((ContainerData*)l->data)->row;
    	p = gtk_tree_row_reference_get_path(r);
    } else {
    	p = gtk_tree_path_new_from_indices(0, -1);
    }

    GtkTreeIter* piter;
    if(!as_child) {
    	if(gtk_tree_path_get_depth(p)>1) {
    		printf("A\n");
    		gtk_tree_path_up(p);
    		XALLOC(piter, GtkTreeIter, 1);
	        gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), piter, p);
    	} else {
    		printf("B\n");
    		piter = NULL;
    	}
	} else {
   		printf("C\n");
		XALLOC(piter, GtkTreeIter, 1);
        gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), piter, p);    	
    }
    gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &iter, piter);
    if(as_child)
    	gtk_tree_view_expand_row(tabster.tabtree, p, FALSE);



    // if(as_child) {
	   //  gint parent_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
    //     GtkTreeRowReference* r = ((ContainerData*)g_list_nth(tabster.socket_list, parent_num)->data)->row;
    //     GtkTreePath* p = gtk_tree_row_reference_get_path(r);
    //     GtkTreeIter piter;
    //     gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &piter, p);
    //     gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &iter, &piter);
    //     gtk_tree_view_expand_row(tabster.tabtree, p, FALSE);
    // } else
    // 	gtk_tree_store_append(GTK_TREE_STORE(tabster.tabmodel), &iter, NULL);

	gtk_tree_path_free(p);
    p = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &iter);

    GtkTreeRowReference* ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(tabster.tabmodel), p);
	gtk_tree_path_free(p);

 //    GtkTreeIter piter;
	// GtkTreePath* parent=NULL;
	// if(gtk_tree_model_iter_parent(GTK_TREE_MODEL(tabster.tabmodel), &piter, &iter))
	// parent = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &piter);

 //    int i = 0;
 //    for(GList* l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
 //        ContainerData* cd = (ContainerData*)l->data;

 //        GtkTreeRowReference* r = cd->row;
 //        GtkTreePath* p2 = gtk_tree_row_reference_get_path(r);

 //        if (parent == NULL)
 //            break;

 //        if (!gtk_tree_path_compare(p2, parent)) {
 //            // uzin->SetPNum(i);
 //            // uzin->SetParent(uz);
 //            // uz->AddChild(uzin);
 //            break;
 //        }
 //        i++;
 //    }

    // uzblinstances = g_list_append(uzblinstances, uzin);
    // totaltabs++;
    // uzin->SetNum(totaltabs);

    // if (save)
    //     SaveSession();
    // UpdateTablist();
    return ref;
}

ContainerData* setup_new_socket_for_plug() {
	ContainerData *cd;
	static int __id;

	XALLOC(cd, ContainerData, 1);

	cd->id = ++__id;
	cd->socket = gtk_socket_new();

	return cd;
}

int spawn(gchar* cmd, int socket) {
	gchar* xcmd = g_strdup_printf(cmd, socket);
    gint argc;
    gchar** argv = NULL;
    int pid;

    g_shell_parse_argv(xcmd, &argc, &argv, NULL);
    GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH );//| G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL
    g_spawn_async(NULL, argv, NULL, flags, NULL, NULL, &pid, NULL);

    g_free(xcmd);
    g_strfreev(argv);

    return pid;
}

gint by_pid(gconstpointer data, gconstpointer pid) {
	return ((ContainerData*)data)->pid==*(int*)pid ? 0 : 1;
}
gint by_widget(gconstpointer data, gconstpointer widget) {
	return ((ContainerData*)data)->socket==(GtkWidget*)widget ? 0 : 1;
}

void set_page(gint i) {
	gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), i);

    GtkTreeIter iter;
    GList* l = g_list_nth(tabster.socket_list, i);
    if(!l)
    	return;
    ContainerData* cd = (ContainerData*)l->data;
    GtkTreeRowReference* r = cd->row;
    GtkTreePath* p = gtk_tree_row_reference_get_path(r);
    gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, p);

    GtkTreeSelection* sel = gtk_tree_view_get_selection(tabster.tabtree);
    gtk_tree_selection_select_iter(sel, &iter);
}

void linear_step(int dir) {
	gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
	GList* l = g_list_nth(tabster.socket_list, page);
	if(l) {
	    GtkTreeIter iter;
	    ContainerData* cd = (ContainerData*)l->data;
		GtkTreeRowReference* r = cd->row;
		GtkTreePath* path = gtk_tree_row_reference_get_path(r);
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
				printf("A %d, %s\n", gtk_tree_path_get_depth(path), gtk_tree_path_to_string(path));
				if(gtk_tree_path_get_depth(path)>1) {
					printf("C\n");
					gtk_tree_path_up(path);
				} else {
					gint lc = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tabster.tabmodel), NULL) - 1;
					gtk_tree_path_free(path);
					path = gtk_tree_path_new_from_indices(lc, -1);					
				}
			}
		}

	    int i = 0;
	    for(GList* l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
	        ContainerData* cd = (ContainerData*)l->data;

	        GtkTreeRowReference* r = cd->row;
	        GtkTreePath* p = gtk_tree_row_reference_get_path(r);

	        if(!gtk_tree_path_compare(path, p)) {
	            set_page(i);
	            break;
	        }
	        i++;
	    }
		gtk_tree_path_free(path);
	}
}

gboolean checkfifo(gpointer data) {
    int r, c = 0;
    for(;;) {
    	r = read(tabster.fifofd, tabster.fifobuf + c, 1);
    	if(!r || tabster.fifobuf[c]=='\n' || c>1022)
    		break;
    	c++;
    }

    if(c) {
    	tabster.fifobuf[c] = '\0';
        printf("-%s-\n", tabster.fifobuf);
        gchar** cmd = g_strsplit(tabster.fifobuf, " ", 2);
    	g_strstrip(cmd[0]);
	    if(cmd[1]) g_strstrip(cmd[1]);
	    
	    if(!g_strcmp0(cmd[0], "new")) {
			ContainerData* cd = setup_new_socket_for_plug();	    	
			cd->row = new_tab_page(GTK_WIDGET(cd->socket), TRUE, FALSE);
	    	cd->pid = spawn(cmd[1], gtk_socket_get_id(GTK_SOCKET(cd->socket)));
			tabster.socket_list = g_list_append(tabster.socket_list, cd);
	    }

	    if(!g_strcmp0(cmd[0], "cnew")) {
			ContainerData* cd = setup_new_socket_for_plug();	    	
			cd->row = new_tab_page(GTK_WIDGET(cd->socket), TRUE, TRUE);
	    	cd->pid = spawn(cmd[1], gtk_socket_get_id(GTK_SOCKET(cd->socket)));
			tabster.socket_list = g_list_append(tabster.socket_list, cd);
	    }

	    if(!g_strcmp0(cmd[0], "bnew")) {
			ContainerData* cd = setup_new_socket_for_plug();	    	
			cd->row = new_tab_page(GTK_WIDGET(cd->socket), TRUE, FALSE);
	    	cd->pid = spawn(cmd[1], gtk_socket_get_id(GTK_SOCKET(cd->socket)));
			tabster.socket_list = g_list_append(tabster.socket_list, cd);
	    }

	    if(!g_strcmp0(cmd[0], "bcnew")) {
	    }

	    if(!g_strcmp0(cmd[0], "tabtitle")) {
	        gchar** lbl = g_strsplit(cmd[1], " ", 2);
	        int pid = atoi(lbl[0]);
	        GList* l = g_list_find_custom(tabster.socket_list, &pid, by_pid);
	        if(l) {
		        // if(strlen(lbl[1]) > 20)
		        // 	lbl[1][20] = '\0';
		        GtkTreeIter iter;
	        	ContainerData* cd = (ContainerData*)l->data;
	        	// gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(tabster.notebook), GTK_WIDGET(((ContainerData*)l->data)->socket), lbl[1]);
		        GtkTreeRowReference* r = cd->row;
        		GtkTreePath* p = gtk_tree_row_reference_get_path(r);
        		gtk_tree_model_get_iter(GTK_TREE_MODEL(tabster.tabmodel), &iter, p);

        		//sprintf(str, "%d: %s", i++, uz->GetTitle());
        		gtk_tree_store_set(GTK_TREE_STORE(tabster.tabmodel), &iter, 0, lbl[1], -1);

	        }
			g_strfreev(lbl);	        
	    }

	    if(!g_strcmp0(cmd[0], "taburi")) {
	        gchar** lbl = g_strsplit(cmd[1], " ", 2);
	        int pid = atoi(lbl[0]);
	        GList* l = g_list_find_custom(tabster.socket_list, &pid, by_pid);
	        if(l)
	        	((ContainerData*)l->data)->uri = g_strdup(lbl[1]); 
			g_strfreev(lbl);	        
	    }

	    if(!g_strcmp0(cmd[0], "prev")) {
	    	linear_step(STEP_PREV);
	    }

	    if(!g_strcmp0(cmd[0], "next")) {
	    	linear_step(STEP_NEXT);
	    }

	    if(!g_strcmp0(cmd[0], "close")) {
	    	gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
			GList* l = g_list_nth(tabster.socket_list, page);
			if(l) {
				ContainerData* cd = l->data;
			    g_spawn_close_pid(cd->pid);
			    gtk_notebook_remove_page(GTK_NOTEBOOK(tabster.notebook), page);
			}
	    }

	    if(!g_strcmp0(cmd[0], "treeclose")) {
	    }

	    if(!g_strcmp0(cmd[0], "goto")) {
	    	gint p = atoi(cmd[1]);
	    	//gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), p);
	    	set_page(p);
	    }

	    if(!g_strcmp0(cmd[0], "move")) {
	    	gint p = atoi(cmd[1]);
	    	gtk_notebook_reorder_child(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook))), p);
	    }

	    if(!g_strcmp0(cmd[0], "attach")) {	
	    }

	    if(!g_strcmp0(cmd[0], "hidetree")) {
	    	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), FALSE);
	    }

	    if(!g_strcmp0(cmd[0], "showtree")) {
	    	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), TRUE);
	    }

	    g_strfreev(cmd);
	    *tabster.fifobuf = '\0';
	}

    return TRUE;
}

void row_clicked(GtkTreeView *view, gpointer data) {
    GtkTreeIter iter;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(tabster.tabtree);
    gtk_tree_selection_get_selected(sel, NULL, &iter);
    GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(tabster.tabmodel), &iter);

    int i = 0;
    for(GList* l = tabster.socket_list; l != NULL; l = g_list_next(l)) {
        // UzblInstance* uz = (UzblInstance*)l->data;
        ContainerData* cd = (ContainerData*)l->data;

        GtkTreeRowReference* r = cd->row;
        GtkTreePath* p = gtk_tree_row_reference_get_path(r);

        if(!gtk_tree_path_compare(path, p)) {
            set_page(i);
            break;
        }
        i++;
    }
}

void setup_window() {
	/* FIXME - create setup_signals() */
	tabster.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(tabster.window, "delete-event", gtk_main_quit, NULL);
	gtk_window_set_default_size(GTK_WINDOW(tabster.window), 800, 600);





    tabster.tabtree = GTK_TREE_VIEW(gtk_tree_view_new());
    // GtkScrolledWindow* tabscroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    // gtk_scrolled_window_set_policy(tabscroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    // gtk_container_add(GTK_CONTAINER(tabscroll), GTK_WIDGET(tabster.tabtree));
    gtk_widget_set_can_focus(GTK_WIDGET(tabster.tabtree), FALSE);
    tabster.tabmodel = gtk_tree_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(tabster.tabtree, GTK_TREE_MODEL(tabster.tabmodel));

    //GtkTreeIter iter;
    GtkCellRenderer* trenderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(tabster.tabtree, -1, "", trenderer, "text", 0, NULL);
    gtk_tree_view_set_headers_visible(tabster.tabtree, FALSE);
    g_signal_connect(tabster.tabtree, "cursor-changed", G_CALLBACK(row_clicked), NULL);










	tabster.notebook = gtk_notebook_new();

	/* GtkNotebook options. */
	gtk_notebook_popup_enable(GTK_NOTEBOOK(tabster.notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabster.notebook), TRUE);
	gtk_notebook_set_tab_border(GTK_NOTEBOOK(tabster.notebook), 1);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK(tabster.notebook), GTK_POS_LEFT);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook), FALSE);

	// gtk_container_add(GTK_CONTAINER(tabster.window), tabster.notebook);




    GtkPaned* pane = GTK_PANED(gtk_hpaned_new());
    gtk_paned_set_position(pane, 200);
    gtk_paned_add1(pane, GTK_WIDGET(tabster.tabtree));
    gtk_paned_add2(pane, GTK_WIDGET(tabster.notebook));
	gtk_container_add(GTK_CONTAINER(tabster.window), GTK_WIDGET(pane));
	



	gtk_widget_show_all(tabster.window);

	g_signal_connect(tabster.notebook, "page-removed", G_CALLBACK(page_removed_cb), (gpointer)&tabster);
}

int main(int argc, char **argv) {
	gboolean version = FALSE;
	GError* error = NULL;
	int pid = getpid();
	gchar* fdfn = g_strdup_printf("/tmp/tabster%d", pid);
	gchar* env_pid = g_strdup_printf("TABSTER_PID=%d", pid);
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
