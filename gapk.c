#include <stdlib.h>
#include <gtk/gtk.h>

struct apk_window {
	GtkWidget *mainwin;
	GtkWidget *vbox;
	GtkWidget *progress;
	GtkWidget *buttonbox;
	GtkWidget *button;
};

static gboolean progress_io_cb(GIOChannel *io, GIOCondition condition, gpointer data)
{
	GtkWidget *progress = data;
	GError *err = NULL;
	gchar *buf;
	gsize len;
	GIOStatus status;
	status = g_io_channel_read_line(io, &buf, &len, NULL, &err);
	if (buf != NULL) {
		float done, total;
		if (sscanf(buf, "%f/%f", &done, &total) == 2)
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), done/total);
		g_free(buf);
	}
	return TRUE;
}

struct apk_window win;

struct apk_window *win_init(int progress_fd)
{

	GIOChannel *io;

	win.mainwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(win.mainwin), 10);
	g_signal_connect(win.mainwin, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	win.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

	win.progress = gtk_progress_bar_new();
	gtk_widget_set_size_request(win.progress, 300, -1);
	gtk_box_pack_start(GTK_BOX(win.vbox), win.progress, FALSE, FALSE, 0);
	gtk_widget_show(win.progress);

	win.buttonbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	win.button = gtk_button_new_with_label("Close");
	gtk_widget_set_sensitive(win.button, FALSE);
	gtk_box_pack_start(GTK_BOX(win.buttonbox), win.button, FALSE, FALSE, 0);
	g_signal_connect(win.button, "clicked", G_CALLBACK(gtk_main_quit), NULL);
	gtk_box_pack_start(GTK_BOX(win.vbox), win.buttonbox, FALSE, FALSE, 0);
	gtk_widget_show(win.buttonbox);
	gtk_widget_show(win.button);

	gtk_container_add(GTK_CONTAINER(win.mainwin), win.vbox);
	gtk_widget_show(win.vbox);

	gtk_widget_show(win.mainwin);

	io = g_io_channel_unix_new(progress_fd);
	g_printf("watching fd %i\n", progress_fd);
	g_io_add_watch(io, G_IO_IN, progress_io_cb, win.progress);

	return &win;
}

void child_setup(gpointer data)
{
	int *fd = (int *)data;
	close(*fd);
}

void child_watch_cb(GPid pid, gint status, gpointer data)
{
	struct apk_window *win = (struct apk_window *)data;
	gtk_widget_set_sensitive(win->button, TRUE);
	g_spawn_close_pid(pid);
}

int main(int argc, char *argv[])
{
	char **apk_argv;
	int progress_pipe[2];
	int i, n = 0;
	char fd_str[32];
	GError *err = NULL;
	GPid child_pid;

	gtk_init(&argc, &argv);

	if (pipe(progress_pipe) < 0)
		return 1;

	apk_argv = g_malloc(sizeof(char *) * (argc + 4));
	if (argv == NULL)
			return 2;
	apk_argv[n++] = "apk";
	apk_argv[n++] = "--progress-fd";
	snprintf(fd_str, sizeof(fd_str)-1, "%i", progress_pipe[1]);
	apk_argv[n++] = fd_str;
	apk_argv[n++] = "--no-progress";
	for (i = 0; i< argc-1; i++)
		apk_argv[i + n] = argv[i+1];
	apk_argv[i + n] = NULL;

	win_init(progress_pipe[0]);
	g_spawn_async(NULL, apk_argv, NULL, G_SPAWN_LEAVE_DESCRIPTORS_OPEN
			| G_SPAWN_SEARCH_PATH | G_SPAWN_CHILD_INHERITS_STDIN
			| G_SPAWN_DO_NOT_REAP_CHILD, child_setup,
			&progress_pipe[0], &child_pid, &err);
	close(progress_pipe[1]);
	g_child_watch_add(child_pid, child_watch_cb, &win);

	gtk_main();
	return 0;
}
