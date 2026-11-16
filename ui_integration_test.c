#define main chess_application_main
#include "main.c"
#undef main
#include <assert.h>
#include <glib/gstdio.h>

static char *system_menu_marker;
static gboolean exit_flow_completed;
static gboolean finish_exit_test(gpointer unused);
static GtkAllocation closed_board;

static void assert_board_fixed(void) {
    GtkAllocation size;
    while (gtk_events_pending()) gtk_main_iteration();
    gtk_widget_get_allocation(app.board, &size);
    assert(size.x == closed_board.x && size.y == closed_board.y);
    assert(size.width == closed_board.width && size.height == closed_board.height);
}

static void widget_button_event(GtkWidget *widget, GdkEventType type, double x, double y) {
    GdkEvent *event = gdk_event_new(type);
    event->button.window = g_object_ref(gtk_widget_get_window(widget));
    event->button.button = 1;
    event->button.x = x;
    event->button.y = y;
    gtk_propagate_event(widget, event);
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

static void assert_compact_menu(double closed_cell) {
    const char *labels[] = { "新局", "悔棋", "退出" };
    GtkAllocation panel_size, board_size, first_button;
    GList *buttons, *item;
    double left, top, cell;
    int index = 0;
    while (gtk_events_pending()) gtk_main_iteration();
    assert(gtk_widget_get_visible(app.menu_panel));
    assert(GTK_IS_HBOX(app.menu_actions));
    buttons = gtk_container_get_children(GTK_CONTAINER(app.menu_actions));
    assert(g_list_length(buttons) == 3);
    for (item = buttons; item != NULL; item = item->next, index++) {
        GtkWidget *button = GTK_WIDGET(item->data);
        GtkAllocation size;
        assert(GTK_IS_BUTTON(button) && gtk_widget_get_visible(button));
        assert(strcmp(gtk_button_get_label(GTK_BUTTON(button)), labels[index]) == 0);
        gtk_widget_get_allocation(button, &size);
        assert(size.height >= 80);
        if (index == 0) {
            first_button = size;
        } else {
            assert(size.y == first_button.y && size.height == first_button.height);
            /* GtkHBox assigns the integer division remainder to its final child. */
            assert(size.width >= first_button.width - 2 && size.width <= first_button.width + 2);
        }
    }
    g_list_free(buttons);
    gtk_widget_get_allocation(app.menu_panel, &panel_size);
    gtk_widget_get_allocation(app.board, &board_size);
    g_print("Open menu allocation: %d px; board height: %d px\n", panel_size.height, board_size.height);
    assert(panel_size.height > 80 && panel_size.height <= 160);
    assert_board_fixed();
    board_geometry(app.board, &left, &top, &cell);
    assert(fabs(cell - closed_cell) < 0.01);
}

static gboolean exercise_ui(gpointer unused) {
    GtkAllocation size;
    double closed_cell;
    AppMode previous_mode;
    char *previous_path;
    (void)unused;
    assert(!gtk_widget_get_visible(app.menu_panel));
    gtk_widget_get_allocation(app.board, &size);
    closed_board = size;
    g_print("Board allocation: %d x %d\n", size.width, size.height);
    assert(size.width >= 1000 && size.height >= 1250);
    assert(strcmp(gtk_label_get_text(GTK_LABEL(app.status)), "红方走棋") == 0);
    {
        double left, top;
        board_geometry(app.board, &left, &top, &closed_cell);
    }
    {
        GdkPixbuf *cannon = app.piece_images[XIANGQI_BLACK][XIANGQI_CANNON];
        const guchar *fire_radical = gdk_pixbuf_get_pixels(cannon) +
            60 * gdk_pixbuf_get_rowstride(cannon) + 60 * gdk_pixbuf_get_n_channels(cannon);
        assert(fire_radical[0] < 30 && fire_radical[1] < 30 && fire_radical[2] < 30);
    }
    capture("board.png");
    widget_button_event(app.status_panel, GDK_BUTTON_PRESS, 500, 28);
    widget_button_event(app.status_panel, GDK_BUTTON_RELEASE, 500, 28);
    assert(gtk_widget_get_visible(app.menu_panel));
    assert_board_fixed();
    hide_menu();
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
    {
        double left, top, cell;
        const double bottom_x[] = { 8, size.width / 2.0, size.width - 8 };
        int i;
        tap_square(6, 0);
        for (i = 0; i < 3; i++) {
            int selected = app.game.selected_id;
            button_event(GDK_BUTTON_PRESS, bottom_x[i], size.height - 8);
            assert(!gtk_widget_get_visible(app.menu_panel));
            button_event(GDK_BUTTON_RELEASE, bottom_x[i], size.height - 8);
            assert(gtk_widget_get_visible(app.menu_panel));
            assert(app.game.history_len == 0 && app.game.selected_id == selected);
            hide_menu();
            while (gtk_events_pending()) gtk_main_iteration();
            button_event(GDK_BUTTON_PRESS, bottom_x[i], 80);
            button_event(GDK_BUTTON_RELEASE, bottom_x[i], 80);
            assert(gtk_widget_get_visible(app.menu_panel));
            assert_board_fixed();
            hide_menu();
            while (gtk_events_pending()) gtk_main_iteration();
        }
        app.mode = MODE_PLAY_BLACK;
        button_event(GDK_BUTTON_PRESS, size.width / 2, size.height - 8);
        button_event(GDK_BUTTON_RELEASE, size.width / 2, size.height - 8);
        assert(gtk_widget_get_visible(app.menu_panel) && app.game.history_len == 0);
        hide_menu();
        app.mode = MODE_TWO_PLAYER;
        app.game.game_over = true;
        button_event(GDK_BUTTON_PRESS, size.width / 2, size.height - 8);
        button_event(GDK_BUTTON_RELEASE, size.width / 2, size.height - 8);
        assert(gtk_widget_get_visible(app.menu_panel) && app.game.history_len == 0);
        hide_menu();
        app.game.game_over = false;
        while (gtk_events_pending()) gtk_main_iteration();
        board_geometry(app.board, &left, &top, &cell);
        button_event(GDK_BUTTON_PRESS, left, top + 9.5 * cell - 1);
        button_event(GDK_BUTTON_RELEASE, left, top + 9.5 * cell - 1);
        assert(!gtk_widget_get_visible(app.menu_panel));
        assert(app.game.selected_id == xiangqi_piece_at(&app.game, 9, 0));
    }
    tap_square(6, 0);
    assert(app.game.selected_id >= 0);
    button_event(GDK_BUTTON_PRESS, size.width / 2, size.height - 15);
    assert(app.game.history_len == 0);
    button_event(GDK_BUTTON_RELEASE, size.width / 2, size.height - 200);
    assert(gtk_widget_get_visible(app.menu_panel));
    assert(app.game.history_len == 0);
    assert_compact_menu(closed_cell);
    capture("menu.png");
    widget_button_event(app.menu_panel, GDK_BUTTON_PRESS, 8, 8);
    widget_button_event(app.menu_panel, GDK_BUTTON_RELEASE, 8, 108);
    assert(!gtk_widget_get_visible(app.menu_panel));
    show_menu();
    tap_square(5, 0);
    assert(!gtk_widget_get_visible(app.menu_panel));
    assert(app.game.history_len == 0);
    tap_square(5, 0);
    assert(app.game.history_len == 1);

    show_menu();
    previous_mode = app.mode;
    show_new_game_cb(NULL, NULL);
    assert_board_fixed();
    assert(!gtk_widget_get_visible(app.difficulty_panel));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_PLAY_BLACK]), TRUE);
    assert(gtk_widget_get_visible(app.difficulty_panel));
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
        assert(gtk_toggle_button_get_mode(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_PLAY_BLACK])));
        assert(gtk_widget_get_style(app.mode_choices[MODE_PLAY_BLACK])->bg[GTK_STATE_ACTIVE].red == 65535);
    }
    capture("newgame.png");
    assert(app.mode == previous_mode && app.game.history_len == 1);
    menu_back_cb(NULL, NULL);
    assert(app.mode == previous_mode && app.game.history_len == 1);
    assert_compact_menu(closed_cell);
    hide_menu();

    assert(save_game());
    assert(g_file_test(app.save_path, G_FILE_TEST_IS_REGULAR));
    undo_cb(NULL, NULL);
    assert(app.game.history_len == 0);
    assert(load_game());
    assert(app.game.history_len == 1);
    app.save_protected = TRUE;
    assert(load_game());
    assert(!app.save_protected);
    {
        char *legacy;
        assert(g_file_get_contents(app.save_path, &legacy, NULL, NULL));
        *strstr(legacy, "settings") = '\0';
        assert(g_file_set_contents(app.save_path, legacy, -1, NULL));
        assert(load_game());
        assert(app.game.history_len == 1);
        {
            char *demo = g_strconcat(legacy, "settings 3 1\n", NULL);
            assert(g_file_set_contents(app.save_path, demo, -1, NULL));
            assert(load_game());
            assert(app.mode == MODE_PLAY_RED && app.game.history_len == 1);
            g_free(demo);
            if (app.ai_source != 0) {
                g_source_remove(app.ai_source);
                app.ai_source = 0;
            }
        }
        g_free(legacy);
    }
    app.game.game_over = true;
    app.game.winner = XIANGQI_RED;
    previous_path = app.save_path;
    app.save_path = "/dev/null/game.save";
    assert(!save_game());
    assert(strstr(gtk_label_get_text(GTK_LABEL(app.status)), "保存失败") != NULL);
    quit_cb(NULL, NULL);
    app.save_path = previous_path;
    g_idle_add(finish_exit_test, NULL);
    return FALSE;
}

static gboolean finish_exit_test(gpointer unused) {
    (void)unused;
    exit_flow_completed = TRUE;
    app.save_protected = TRUE;
    show_menu();
    show_new_game_cb(NULL, NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_PLAY_RED]), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.level_choices[XIANGQI_AI_EASY]), TRUE);
    new_game_cb(NULL, NULL);
    assert(app.mode == MODE_PLAY_RED && app.level == XIANGQI_AI_EASY);
    assert(app.game.history_len == 0 && !gtk_widget_get_visible(app.menu_panel));
    assert(!app.save_protected);
    app.mode = MODE_TWO_PLAYER;
    app.level = XIANGQI_AI_HARD;
    tap_square(6, 0);
    tap_square(5, 0);
    assert(app.game.history_len == 1);
    g_remove(app.save_path);
    puts("UI passed: compact three-button menu, gestures, settings, legacy save, failed exit kept alive, full game auto-save");
    quit_cb(NULL, NULL);
    return FALSE;
}

static gboolean check_resume_message_expired(gpointer unused) {
    (void)unused;
    assert(app.message[0] == '\0');
    assert(strcmp(gtk_label_get_text(GTK_LABEL(app.status)), "黑方走棋") == 0);
    assert_board_fixed();
    puts("Resume notice passed: success shown for two seconds, then current turn; board unchanged");
    quit_cb(NULL, NULL);
    return FALSE;
}

static gboolean check_auto_resume(gpointer unused) {
    (void)unused;
    assert(app.game.history_len == 1);
    assert(app.mode == MODE_TWO_PLAYER && app.level == XIANGQI_AI_HARD);
    assert(!gtk_widget_get_visible(app.menu_panel));
    assert(gtk_widget_get_visible(app.status));
    assert(strcmp(gtk_label_get_text(GTK_LABEL(app.status)), "已恢复上次对局") == 0);
    gtk_widget_get_allocation(app.board, &closed_board);
    puts("Auto resume passed: history, playing side, difficulty");
    g_timeout_add(2100, check_resume_message_expired, NULL);
    return FALSE;
}

static gboolean check_failed_resume(gpointer unused) {
    (void)unused;
    assert(app.game.history_len == 0);
    assert(app.save_protected);
    quit_cb(NULL, NULL);
    return FALSE;
}

int main(int argc, char **argv) {
    char *directory = g_dir_make_tmp("chess-ui-native-XXXXXX", NULL);
    char *command = g_build_filename(directory, "lipc-set-prop", NULL);
    char *path = g_strconcat(directory, ":", g_getenv("PATH"), NULL);
    char *data_directory = g_build_filename(directory, "data", NULL);
    char *save_path = g_build_filename(data_directory, "game.save", NULL);
    char *log_path = g_build_filename(data_directory, "app.log", NULL);
    int result;
    gboolean resume = argc > 1 && strcmp(argv[1], "--resume-check") == 0;
    gboolean failed_resume = argc > 1 && strcmp(argv[1], "--failed-resume-check") == 0;
    gboolean child_check = resume || failed_resume;
    system_menu_marker = g_build_filename(directory, "requested", NULL);
    assert(g_file_set_contents(command, "#!/bin/sh\n: > \"$CHESS_TEST_SYSTEM_MENU_MARKER\"\n", -1, NULL));
    assert(g_chmod(command, 0755) == 0);
    g_setenv("PATH", path, TRUE);
    g_setenv("CHESS_TEST_SYSTEM_MENU_MARKER", system_menu_marker, TRUE);
    if (!child_check) g_setenv("EXACT_CHINESECHESS_DATA_DIR", data_directory, TRUE);
    g_timeout_add(300, resume ? check_auto_resume : failed_resume ? check_failed_resume : exercise_ui, NULL);
    result = chess_application_main(argc, argv);
    if (!child_check) {
        gchar *resume_argv[] = { argv[0], "--resume-check", NULL };
        char *corrupted, *after;
        int status;
        assert(exit_flow_completed && g_file_test(save_path, G_FILE_TEST_IS_REGULAR));
        assert(g_spawn_sync(NULL, resume_argv, NULL, 0, NULL, NULL, NULL, NULL, &status, NULL));
        assert(status == 0);
        assert(g_file_get_contents(save_path, &corrupted, NULL, NULL));
        *strstr(corrupted, "settings") = '\0';
        {
            char *invalid = g_strconcat(corrupted, "settings 999 999\n", NULL);
            g_free(corrupted);
            corrupted = invalid;
        }
        assert(g_file_set_contents(save_path, corrupted, -1, NULL));
        resume_argv[1] = "--failed-resume-check";
        assert(g_spawn_sync(NULL, resume_argv, NULL, 0, NULL, NULL, NULL, NULL, &status, NULL));
        assert(status == 0);
        assert(g_file_get_contents(save_path, &after, NULL, NULL));
        assert(strcmp(after, corrupted) == 0);
        puts("Failed resume passed: original save preserved on exit");
        g_free(corrupted);
        g_free(after);
    }
    g_remove(save_path);
    g_remove(log_path);
    g_rmdir(data_directory);
    g_free(data_directory);
    g_free(save_path);
    g_free(log_path);
    g_remove(system_menu_marker);
    g_remove(command);
    g_rmdir(directory);
    g_free(system_menu_marker);
    g_free(command);
    g_free(path);
    g_free(directory);
    return result;
}
