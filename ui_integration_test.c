#define main chess_application_main
#include "main.c"
#undef main
#include <assert.h>
#include <glib/gstdio.h>

static char *system_menu_marker;

static void widget_button_event(GtkWidget *widget, GdkEventType type, double x, double y) {
    GdkEvent *event = gdk_event_new(type);
    event->button.window = g_object_ref(gtk_widget_get_window(widget));
    event->button.button = 1;
    event->button.x = x;
    event->button.y = y;
    gtk_widget_event(widget, event);
    gdk_event_free(event);
}

static void button_event(GdkEventType type, double x, double y) {
    widget_button_event(app.board, type, x, y);
}

static void tap_square(int row, int col) {
    double left, top, cell;
    board_geometry(app.board, &left, &top, &cell);
    button_event(GDK_BUTTON_PRESS, left + col * cell, top + row * cell);
    button_event(GDK_BUTTON_RELEASE, left + col * cell, top + row * cell);
}

static void capture(const char *name) {
    const char *directory = g_getenv("CHESS_TEST_SCREENSHOTS");
    GtkAllocation size;
    GdkPixbuf *pixels;
    char *path;
    if (!directory) return;
    while (gtk_events_pending()) gtk_main_iteration();
    gtk_widget_get_allocation(app.window, &size);
    pixels = gdk_pixbuf_get_from_drawable(NULL, gtk_widget_get_window(app.window),
                                        NULL, 0, 0, 0, 0, size.width, size.height);
    assert(pixels);
    path = g_build_filename(directory, name, NULL);
    assert(gdk_pixbuf_save(pixels, path, "png", NULL, NULL));
    g_free(path);
    g_object_unref(pixels);
}

static gboolean exercise_ui(gpointer unused) {
    GtkAllocation size;
    AppMode previous_mode;
    char *previous_path;
    (void)unused;
    assert(!gtk_widget_get_visible(app.menu_panel));
    gtk_widget_get_allocation(app.board, &size);
    g_print("Board allocation: %d x %d\n", size.width, size.height);
    assert(size.width >= 1000 && size.height >= 1250);
    {
        GdkPixbuf *cannon = app.piece_images[XIANGQI_BLACK][XIANGQI_CANNON];
        const guchar *fire_radical = gdk_pixbuf_get_pixels(cannon) +
            60 * gdk_pixbuf_get_rowstride(cannon) + 60 * gdk_pixbuf_get_n_channels(cannon);
        assert(fire_radical[0] < 30 && fire_radical[1] < 30 && fire_radical[2] < 30);
    }
    capture("board.png");
    widget_button_event(app.window, GDK_BUTTON_PRESS, 500, 12);
    widget_button_event(app.window, GDK_BUTTON_RELEASE, 500, 200);
    {
        gint64 deadline = g_get_monotonic_time() + 2000000;
        while (!g_file_test(system_menu_marker, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < deadline)
            g_usleep(10000);
        assert(g_file_test(system_menu_marker, G_FILE_TEST_EXISTS));
        assert(!gtk_widget_get_visible(app.menu_panel) && app.game.history_len == 0);
    }
    app.mode = MODE_TWO_PLAYER;
    new_game();
    tap_square(6, 0);
    assert(app.game.selected_id >= 0);
    button_event(GDK_BUTTON_PRESS, size.width / 2, size.height - 15);
    assert(app.game.history_len == 0);
    button_event(GDK_BUTTON_RELEASE, size.width / 2, size.height - 200);
    assert(gtk_widget_get_visible(app.menu_panel));
    assert(app.game.history_len == 0);
    capture("menu.png");
    tap_square(5, 0);
    assert(!gtk_widget_get_visible(app.menu_panel));
    assert(app.game.history_len == 0);
    tap_square(5, 0);
    assert(app.game.history_len == 1);

    show_menu();
    previous_mode = app.mode;
    show_new_game_cb(NULL, NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_PLAY_BLACK]), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.level_choices[XIANGQI_AI_EASY]), TRUE);
    {
        GtkWidget *label = gtk_bin_get_child(GTK_BIN(app.mode_choices[MODE_PLAY_BLACK]));
        PangoLayout *layout = gtk_label_get_layout(GTK_LABEL(label));
        int width, height;
        pango_cairo_context_set_resolution(gtk_widget_get_pango_context(label), 300);
        pango_layout_context_changed(layout);
        pango_layout_get_pixel_size(layout, &width, &height);
        assert(height <= 48 && width <= 200);
        assert(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_TWO_PLAYER])));
    }
    capture("newgame.png");
    assert(app.mode == previous_mode && app.game.history_len == 1);
    menu_back_cb(NULL, NULL);
    assert(app.mode == previous_mode && app.game.history_len == 1);
    show_history_cb(NULL, NULL);
    history_first_cb(NULL, NULL);
    assert(!is_viewing_latest());
    hide_menu();
    assert(is_viewing_latest());

    save_cb(NULL, NULL);
    assert(g_file_test(app.save_path, G_FILE_TEST_IS_REGULAR));
    undo_cb(NULL, NULL);
    assert(app.game.history_len == 0);
    load_cb(NULL, NULL);
    assert(app.game.history_len == 1);
    app.game.game_over = true;
    app.game.winner = XIANGQI_RED;
    previous_path = app.save_path;
    app.save_path = "/dev/null/game.save";
    save_cb(NULL, NULL);
    assert(strstr(gtk_label_get_text(GTK_LABEL(app.status)), "保存失败") != NULL);
    app.save_path = previous_path;
    show_menu();
    show_new_game_cb(NULL, NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_PLAY_RED]), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.level_choices[XIANGQI_AI_EASY]), TRUE);
    new_game_cb(NULL, NULL);
    assert(app.mode == MODE_PLAY_RED && app.level == XIANGQI_AI_EASY);
    assert(app.game.history_len == 0 && !gtk_widget_get_visible(app.menu_panel));
    puts("UI passed: hidden menu, release-only move, swipe isolation, settings cancel, history exit, save/load");
    gtk_main_quit();
    return FALSE;
}

int main(int argc, char **argv) {
    char *directory = g_dir_make_tmp("chess-ui-native-XXXXXX", NULL);
    char *command = g_build_filename(directory, "lipc-set-prop", NULL);
    char *path = g_strconcat(directory, ":", g_getenv("PATH"), NULL);
    int result;
    system_menu_marker = g_build_filename(directory, "requested", NULL);
    assert(g_file_set_contents(command, "#!/bin/sh\n: > \"$CHESS_TEST_SYSTEM_MENU_MARKER\"\n", -1, NULL));
    assert(g_chmod(command, 0755) == 0);
    g_setenv("PATH", path, TRUE);
    g_setenv("CHESS_TEST_SYSTEM_MENU_MARKER", system_menu_marker, TRUE);
    g_timeout_add(300, exercise_ui, NULL);
    result = chess_application_main(argc, argv);
    g_remove(system_menu_marker);
    g_remove(command);
    g_rmdir(directory);
    g_free(system_menu_marker);
    g_free(command);
    g_free(path);
    g_free(directory);
    return result;
}
