#include <stdlib.h>
#include <gtk/gtk.h>

struct apk_window {
	GtkWidget *mainwin;
	GtkWidget *vbox;
	GtkWidget *textview;
	GtkWidget *progress;
	GtkWidget *buttonbox;
	GtkWidget *button;
	GTimer *timer;
};

static gboolean progress_io_cb(GIOChannel *io, GIOCondition condition,
			       gpointer data)
{
	struct apk_window *win = data;
	GError *err = NULL;
	gchar *buf;
	gsize len;
	GIOStatus status;
	status = g_io_channel_read_line(io, &buf, &len, NULL, &err);
	if (buf != NULL) {
		float done, total;
		if (sscanf(buf, "%f/%f", &done, &total) == 2) {
			if (win->timer == NULL && total > 0)
				win->timer = g_timer_new();
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(win->progress), done/total);
			if (win->timer != NULL) {
				gulong ms;
				gchar *size, *txt;
				size = g_format_size(done / g_timer_elapsed(win->timer, &ms));
				txt = g_strconcat(size, "/s", NULL);

				gtk_progress_bar_set_text(GTK_PROGRESS_BAR(win->progress), txt);
				g_free(size);
				g_free(txt);
			}
		}
		g_free(buf);
	}
	return TRUE;
}

static gboolean output_io_cb(GIOChannel *io, GIOCondition condition,
			     gpointer data)
{
	GtkTextBuffer *viewbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data));
	GtkTextIter iter;
	GError *err = NULL;
	gchar *line;
	gsize len;
	g_io_channel_read_line(io, &line, &len, NULL, &err);
	if (line != NULL) {
		gtk_text_buffer_get_end_iter(viewbuf, &iter);
		gtk_text_buffer_insert(viewbuf, &iter, line, len);
		g_free(line);
		gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(data), &iter, 0.0,
				FALSE, 0, 0);
	}
	return TRUE;
}

struct apk_window win;

struct apk_window *win_init(int progress_fd, int out_fd, int err_fd)
{

	GIOChannel *progress_io, *out_io, *err_io;
	GtkWidget *scrollwin = gtk_scrolled_window_new(NULL, NULL);

	win.mainwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(win.mainwin), 10);
	g_signal_connect(win.mainwin, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	win.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

	gtk_widget_set_size_request(scrollwin, 600, 106);
	win.textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(win.textview), FALSE);
	gtk_container_add(GTK_CONTAINER(scrollwin), win.textview);
	gtk_widget_show(scrollwin);

	gtk_box_pack_start(GTK_BOX(win.vbox), scrollwin, TRUE, TRUE, 0);
	gtk_widget_show(win.textview);

	win.progress = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(win.progress), TRUE);
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

	progress_io = g_io_channel_unix_new(progress_fd);
	win.timer = NULL;
	g_io_add_watch(progress_io, G_IO_IN, progress_io_cb, &win);

	out_io = g_io_channel_unix_new(out_fd);
	g_io_add_watch(out_io, G_IO_IN, output_io_cb, win.textview);

	err_io = g_io_channel_unix_new(err_fd);
	g_io_add_watch(err_io, G_IO_IN, output_io_cb, win.textview);

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

#define PIPE_READ 0
#define PIPE_WRITE 1

int main(int argc, char *argv[])
{
	char **apk_argv;
	int pipe_progress[2], pipe_stdout[2], pipe_stderr[2];
	int i, n = 0;
	char fd_str[32];
	GPid child_pid;

	gtk_init(&argc, &argv);

	if (pipe(pipe_progress) < 0)
		return 1;

	apk_argv = g_malloc(sizeof(char *) * (argc + 4));
	if (argv == NULL)
			return 2;
	apk_argv[n++] = "apk";
	apk_argv[n++] = "--progress-fd";
	snprintf(fd_str, sizeof(fd_str)-1, "%i", pipe_progress[PIPE_WRITE]);
	apk_argv[n++] = fd_str;
	apk_argv[n++] = "--no-progress";
	for (i = 0; i< argc-1; i++)
		apk_argv[i + n] = argv[i+1];
	apk_argv[i + n] = NULL;

	g_spawn_async_with_pipes(NULL, apk_argv, NULL,
			G_SPAWN_LEAVE_DESCRIPTORS_OPEN
				| G_SPAWN_SEARCH_PATH
				| G_SPAWN_CHILD_INHERITS_STDIN
				| G_SPAWN_DO_NOT_REAP_CHILD,
			child_setup, &pipe_progress[PIPE_READ],
			&child_pid,
			NULL, pipe_stdout, pipe_stderr,
			NULL);
	close(pipe_progress[PIPE_WRITE]);
	g_child_watch_add(child_pid, child_watch_cb, &win);

	win_init(pipe_progress[PIPE_READ], pipe_stdout[PIPE_READ],
			pipe_stderr[PIPE_READ]);

	gtk_main();
	return 0;
}
