#define main chess_application_main
#include "main.c"
#undef main
#include <assert.h>

static void button_event(GdkEventType type, double x, double y) {
    GdkEvent *event = gdk_event_new(type);
    event->button.window = g_object_ref(gtk_widget_get_window(app.board));
    event->button.button = 1;
    event->button.x = x;
    event->button.y = y;
    gtk_widget_event(app.board, event);
    gdk_event_free(event);
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
    capture("board.png");
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
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.mode_combo), MODE_PLAY_BLACK);
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
    puts("UI passed: hidden menu, release-only move, swipe isolation, settings cancel, history exit, save/load");
    gtk_main_quit();
    return FALSE;
}

int main(int argc, char **argv) {
    g_timeout_add(300, exercise_ui, NULL);
    return chess_application_main(argc, argv);
}
