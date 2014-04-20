/*
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <glib.h>

struct ContainerData_ {
	int id;
	GtkWidget* socket;
	// GdkNativeWindow xwin_id;
	// gboolean has_plug;
	int pid;
	gchar* uri;
} typedef ContainerData;

// enum {
// 	COMMAND_0,
// 	COMMAND_PRINT_XID
// };

struct Tabster_ {
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *vbox;
	// int next_socket_id;

	GList *socket_list;
	// int current;

	int fifofd;
    char fifobuf[1024];
    //int fp;
} typedef Tabster;

static void die(const char *errstr, ...);
static void page_removed_cb(GtkNotebook *, GtkWidget *, guint, gpointer);
static void new_tab_page(GtkWidget *);
static ContainerData* setup_new_socket_for_plug();
static int spawn(gchar* cmd, int socket);
static void setup_window();
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
	// Tabster *g = (Tabster *)data;
	// GList *temp;

	// for(temp = g->socket_list; g->socket_list ; g->socket_list = g->socket_list->next) {
	// 	if(g->socket_list->data == widget)
	// 		temp = g_list_delete_link(g->socket_list, temp);
	// }

	return;
}

void new_tab_page(GtkWidget *socket) {
	gtk_widget_show(socket);
	gtk_notebook_append_page(GTK_NOTEBOOK(tabster.notebook), socket, NULL);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), -1);

	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(tabster.notebook), socket, TRUE);
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
    GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL );
    g_spawn_async(NULL, argv, NULL, flags, NULL, NULL, &pid, NULL);

    g_free(xcmd);
    g_strfreev(argv);

    return pid;
}

gint by_pid(gconstpointer data, gconstpointer pid) {
	return ((ContainerData*)data)->pid==*(int*)pid ? 0 : 1;
}

gboolean checkfifo(gpointer data) {
    int r, c = 0;
    do {
    	r = read(tabster.fifofd, tabster.fifobuf + c, 1);
    	if(r)
    		c++;
    } while(r && tabster.fifobuf[c-1]!='\n' && c<1024);
    if(c) {
    	tabster.fifobuf[c-1] = '\0';
        printf("-%s-\n", tabster.fifobuf);
        gchar** cmd = g_strsplit(tabster.fifobuf, " ", 2);
    	g_strstrip(cmd[0]);
	    if(cmd[1]) g_strstrip(cmd[1]);
	    
	    if(!g_strcmp0(cmd[0], "new")) {
			ContainerData* cd = setup_new_socket_for_plug();	    	
			new_tab_page(GTK_WIDGET(cd->socket));
	    	cd->pid = spawn(cmd[1], gtk_socket_get_id(GTK_SOCKET(cd->socket)));
			tabster.socket_list = g_list_prepend(tabster.socket_list, cd);
	    }

	    if(!g_strcmp0(cmd[0], "tabtitle")) {
	        gchar** lbl = g_strsplit(cmd[1], " ", 2);
	        int pid = atoi(lbl[0]);
	        GList* l = g_list_find_custom(tabster.socket_list, &pid, by_pid);
	        if(l)
	        	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(tabster.notebook), GTK_WIDGET(((ContainerData*)l->data)->socket), lbl[1]);
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
	    	gtk_notebook_prev_page(GTK_NOTEBOOK(tabster.notebook));
	    }

	    if(!g_strcmp0(cmd[0], "next")) {
	    	gtk_notebook_next_page(GTK_NOTEBOOK(tabster.notebook));
	    }

	    if(!g_strcmp0(cmd[0], "close")) {
	    	gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook));
			GList* l = g_list_nth(tabster.socket_list, page);
			ContainerData* cd = l->data;
		    g_spawn_close_pid(cd->pid);
		    gtk_notebook_remove_page(GTK_NOTEBOOK(tabster.notebook), page);
			g_free(cd->uri);
			g_free(cd);
			tabster.socket_list = g_list_remove(tabster.socket_list, l->data);
	    }

	    if(!g_strcmp0(cmd[0], "goto")) {
	    	ging p = atoi(cmd[1]);
	    	gtk_notebook_set_current_page(GTK_NOTEBOOK(tabster.notebook), p);
	    }

	    if(!g_strcmp0(cmd[0], "move")) {
	    	ging p = atoi(cmd[1]);
	    	gtk_notebook_reorder_child(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabster.notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(tabster.notebook))), p);
	    }

	    if(!g_strcmp0(cmd[0], "attach")) {	
	    }

	    if(!g_strcmp0(cmd[0], "hidetree")) {
	    	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook, FALSE));
	    }

	    if(!g_strcmp0(cmd[0], "showtree")) {
	    	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(tabster.notebook, TRUE));
	    }

	    g_strfreev(cmd);
	    *tabster.fifobuf = '\0';
	}

    return TRUE;
}

void setup_window() {
	/* FIXME - create setup_signals() */
	tabster.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(tabster.window, "delete-event", gtk_main_quit, NULL);
	gtk_window_set_default_size(GTK_WINDOW(tabster.window), 800, 600);

	tabster.notebook = gtk_notebook_new();

	/* GtkNotebook options. */
	gtk_notebook_popup_enable(GTK_NOTEBOOK(tabster.notebook));
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabster.notebook), TRUE);
	gtk_notebook_set_tab_border(GTK_NOTEBOOK(tabster.notebook), 1);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK(tabster.notebook), GTK_POS_LEFT);

	gtk_container_add(GTK_CONTAINER(tabster.window), tabster.notebook);

	gtk_widget_show_all(tabster.window);

	g_signal_connect(tabster.notebook, "page-removed", G_CALLBACK(page_removed_cb), (gpointer)&tabster);
}

int main(int argc, char **argv) {
	gboolean version = FALSE;
	GError *error = NULL;

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

    mkfifo("/tmp/tabster", 0766);
    tabster.fifofd = open("/tmp/tabster", O_NONBLOCK);
    g_timeout_add(100, checkfifo, NULL);

    // printf("%d\n", tabster.fifofd);

	gtk_main();

    close(tabster.fifofd);
    unlink("/tmp/tabster");

	return EXIT_SUCCESS;
}
