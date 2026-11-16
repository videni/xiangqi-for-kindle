#define main chess_application_main
#include "main.c"
#undef main
#include <assert.h>
#include <glib/gstdio.h>

static char *system_menu_marker;
static gboolean exit_flow_completed;
static gboolean finish_exit_test(gpointer unused);
static GtkAllocation closed_board;
static gboolean track_exposes;
static int expose_count;
static GdkRectangle exposed_area;

static int native_output_windows(Window parent) {
    Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window root, owner, *children = NULL;
    unsigned int count = 0, i;
    int output = 0;
    assert(XQueryTree(display, parent, &root, &owner, &children, &count));
    for (i = 0; i < count; i++) {
        XWindowAttributes attributes;
        assert(XGetWindowAttributes(display, children[i], &attributes));
        /* GTK may keep mapped 1x1 helper windows; count actual drawing surfaces. */
        if (attributes.class == InputOutput && attributes.map_state == IsViewable &&
            attributes.width > 1 && attributes.height > 1) output++;
        output += native_output_windows(children[i]);
    }
    if (children) XFree(children);
    return output;
}

static void flush_ui(void) {
    while (gtk_events_pending()) gtk_main_iteration();
    gdk_window_process_all_updates();
    XSync(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), False);
    while (gtk_events_pending()) gtk_main_iteration();
}

static void assert_board_frame(GtkWidget *widget, GtkAllocation *frame, gpointer unused) {
    (void)widget;
    (void)unused;
    if (frame->x != closed_board.x || frame->y != closed_board.y ||
        frame->width != closed_board.width || frame->height != closed_board.height) {
        g_printerr("Transient board allocation: %d,%d %dx%d\n",
                   frame->x, frame->y, frame->width, frame->height);
        assert(FALSE);
    }
}

static void assert_board_fixed(void) {
    GtkAllocation size;
    flush_ui();
    gtk_widget_get_allocation(app.board, &size);
    assert_board_frame(app.board, &size, NULL);
    assert(native_output_windows(GDK_WINDOW_XID(gtk_widget_get_window(app.window))) == 1);
}

static gboolean observe_expose(GtkWidget *widget, GdkEventExpose *event, gpointer unused) {
    (void)widget;
    (void)unused;
    if (track_exposes) {
        GdkRectangle *rectangles = NULL;
        int count = 0;
        expose_count++;
        exposed_area = event->area;
        gdk_region_get_rectangles(event->region, &rectangles, &count);
        assert(count == 1);
        assert(rectangles[0].x == event->area.x && rectangles[0].y == event->area.y);
        assert(rectangles[0].width == event->area.width && rectangles[0].height == event->area.height);
        g_free(rectangles);
    }
    return FALSE;
}

static void begin_menu_expose(void) {
    flush_ui();
    expose_count = 0;
    track_exposes = TRUE;
}

static void end_menu_expose(GtkAllocation expected) {
    flush_ui();
    track_exposes = FALSE;
    if (expose_count != 1 || exposed_area.x != expected.x || exposed_area.y != expected.y ||
        exposed_area.width != expected.width || exposed_area.height != expected.height) {
        g_printerr("Menu expose: count=%d area=%d,%d %dx%d expected=%d,%d %dx%d\n",
                   expose_count, exposed_area.x, exposed_area.y, exposed_area.width, exposed_area.height,
                   expected.x, expected.y, expected.width, expected.height);
        assert(FALSE);
    }
    assert_board_fixed();
}

static void button_event(GdkEventType type, double x, double y) {
    GdkEvent *event = gdk_event_new(type);
    event->button.window = g_object_ref(gtk_widget_get_window(app.board));
    event->button.button = 1;
    event->button.x = x;
    event->button.y = y;
    gtk_propagate_event(app.board, event);
    gdk_event_free(event);
}

static void tap_at(double x, double y) {
    button_event(GDK_BUTTON_PRESS, x, y);
    button_event(GDK_BUTTON_RELEASE, x, y);
}

static void tap_square(int row, int col) {
    double left, top, cell;
    board_geometry(app.board, &left, &top, &cell);
    tap_at(left + col * cell, top + row * cell);
}

static void open_menu_at(double x, double y) {
    XiangqiGame before;
    memcpy(&before, &app.game, sizeof(before));
    assert(app.menu_view == MENU_CLOSED);
    begin_menu_expose();
    button_event(GDK_BUTTON_PRESS, x, y);
    assert(app.menu_view == MENU_CLOSED && expose_count == 0);
    button_event(GDK_BUTTON_RELEASE, x, y);
    assert(app.menu_view == MENU_ACTIONS);
    assert(memcmp(&before, &app.game, sizeof(before)) == 0);
    end_menu_expose(menu_geometry());
}

static void close_menu_at(double x, double y) {
    XiangqiGame before;
    GtkAllocation area = menu_geometry();
    memcpy(&before, &app.game, sizeof(before));
    assert(app.menu_view != MENU_CLOSED);
    begin_menu_expose();
    tap_at(x, y);
    assert(app.menu_view == MENU_CLOSED);
    assert(memcmp(&before, &app.game, sizeof(before)) == 0);
    end_menu_expose(area);
}

static void tap_action(int index) {
    GtkAllocation panel = menu_geometry();
    assert(app.menu_view == MENU_ACTIONS && index >= 0 && index < 3);
    if (index == 0) begin_menu_expose();
    tap_at(panel.x + 16 + (index + 0.5) * (panel.width - 32) / 3.0, panel.y + 92);
    if (index == 0) end_menu_expose(menu_geometry());
}

static void tap_choice(int index, gboolean difficulty) {
    GtkAllocation panel = menu_geometry();
    double width = (panel.width - 32 - 24) / 3.0;
    assert(app.menu_view == MENU_NEW_GAME && index >= 0 && index < 3);
    assert(!difficulty || app.pending_mode != MODE_TWO_PLAYER);
    begin_menu_expose();
    tap_at(panel.x + 16 + index * (width + 12) + width / 2.0,
           panel.y + (difficulty ? 280 : 139));
    {
        GtkAllocation next = menu_geometry();
        if (next.y < panel.y) panel = next;
        end_menu_expose(panel);
    }
}

static void tap_form_button(gboolean start) {
    GtkAllocation panel = menu_geometry();
    double width = (panel.width - 32 - 12) / 2.0;
    assert(app.menu_view == MENU_NEW_GAME);
    if (!start) begin_menu_expose();
    tap_at(panel.x + 16 + (start ? width + 12 : 0) + width / 2.0,
           panel.y + (app.pending_mode == MODE_TWO_PLAYER ? 233 : 374));
    if (!start) end_menu_expose(panel);
}

static void assert_compact_menu(double closed_cell) {
    GtkAllocation panel = menu_geometry();
    double left, top, cell;
    assert(app.menu_view == MENU_ACTIONS);
    assert(panel.x == 24 && panel.width == closed_board.width - 48);
    assert(panel.height == 148 && panel.y + panel.height == closed_board.height - 24);
    assert_board_fixed();
    board_geometry(app.board, &left, &top, &cell);
    assert(fabs(cell - closed_cell) < 0.01);
}

static gboolean exercise_ui(gpointer unused) {
    GtkAllocation size;
    double closed_cell;
    AppMode previous_mode;
    XiangqiAiLevel previous_level;
    char *previous_path;
    (void)unused;
    assert(app.menu_view == MENU_CLOSED);
    gtk_widget_get_allocation(app.board, &size);
    closed_board = size;
    g_signal_connect_after(app.board, "size-allocate", G_CALLBACK(assert_board_frame), NULL);
    g_signal_connect_after(app.board, "expose-event", G_CALLBACK(observe_expose), NULL);
    g_print("Board allocation: %d x %d\n", size.width, size.height);
    assert(size.width >= 1000 && size.height >= 1250);
    assert(strcmp(current_status(), "红方走棋") == 0);
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
    {
        gchar *activate[] = { "./ui-test", "--activate", NULL };
        int status;
        assert(g_spawn_sync(NULL, activate, NULL, 0, NULL, NULL, NULL, NULL, &status, NULL));
        assert(status == 0 && app.game.history_len == 0);
    }
    assert_board_fixed();
    open_menu_at(500, 28);
    assert_compact_menu(closed_cell);
    {
        GtkAllocation panel = menu_geometry();
        close_menu_at(panel.x + 8, panel.y + 92);
        open_menu_at(500, 28);
        panel = menu_geometry();
        close_menu_at(panel.x + panel.width / 2.0, panel.y + 140);
        open_menu_at(500, 28);
    }
    {
        GtkAllocation panel = menu_geometry();
        close_menu_at(panel.x + panel.width / 2.0, panel.y + 30);
    }
    button_event(GDK_BUTTON_PRESS, 500, 12);
    button_event(GDK_BUTTON_RELEASE, 500, 200);
    {
        gint64 deadline = g_get_monotonic_time() + 2000000;
        while (!g_file_test(system_menu_marker, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < deadline)
            g_usleep(10000);
        assert(g_file_test(system_menu_marker, G_FILE_TEST_EXISTS));
        assert(app.menu_view == MENU_CLOSED && app.game.history_len == 0);
    }
    app.mode = MODE_TWO_PLAYER;
    new_game();
    {
        double left, top, cell;
        const double bottom_x[] = { 8, size.width / 2.0, size.width - 8 };
        int i;
        tap_square(6, 0);
        for (i = 0; i < 3; i++) {
            open_menu_at(bottom_x[i], size.height - 8);
            close_menu_at(500, 300);
            open_menu_at(bottom_x[i], 80);
            close_menu_at(500, 300);
        }
        app.mode = MODE_PLAY_BLACK;
        open_menu_at(size.width / 2.0, size.height - 8);
        close_menu_at(500, 300);
        app.mode = MODE_TWO_PLAYER;
        app.game.game_over = true;
        open_menu_at(size.width / 2.0, size.height - 8);
        close_menu_at(500, 300);
        app.game.game_over = false;
        board_geometry(app.board, &left, &top, &cell);
        tap_at(left, top + 9.5 * cell - 1);
        assert(app.menu_view == MENU_CLOSED);
        assert(app.game.selected_id == xiangqi_piece_at(&app.game, 9, 0));
    }
    tap_square(6, 0);
    assert(app.game.selected_id >= 0);
    begin_menu_expose();
    button_event(GDK_BUTTON_PRESS, size.width / 2.0, size.height - 15);
    assert(app.game.history_len == 0 && app.menu_view == MENU_CLOSED);
    button_event(GDK_BUTTON_RELEASE, size.width / 2.0, size.height - 200);
    assert(app.menu_view == MENU_ACTIONS && app.game.history_len == 0);
    end_menu_expose(menu_geometry());
    assert_compact_menu(closed_cell);
    {
        GtkAllocation panel = menu_geometry();
        begin_menu_expose();
        button_event(GDK_BUTTON_PRESS, panel.x + 8, panel.y + 8);
        button_event(GDK_BUTTON_RELEASE, panel.x + 8, panel.y + 108);
        assert(app.menu_view == MENU_CLOSED && app.game.history_len == 0);
        end_menu_expose(panel);
    }
    open_menu_at(500, 28);
    {
        double left, top, cell;
        board_geometry(app.board, &left, &top, &cell);
        close_menu_at(left, top + 5 * cell);
    }
    assert(app.game.history_len == 0);
    tap_square(5, 0);
    assert(app.game.history_len == 1);

    open_menu_at(500, 28);
    previous_mode = app.mode;
    previous_level = app.level;
    tap_action(0);
    assert(app.menu_view == MENU_NEW_GAME && app.pending_mode == MODE_TWO_PLAYER);
    assert(menu_geometry().height == 289);
    assert_board_fixed();
    tap_choice(MODE_PLAY_BLACK, FALSE);
    assert(app.pending_mode == MODE_PLAY_BLACK && menu_geometry().height == 430);
    tap_choice(XIANGQI_AI_EASY, TRUE);
    assert(app.pending_level == XIANGQI_AI_EASY);
    {
        cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(app.board));
        PangoLayout *layout = ui_text_layout(cr, "我执黑", FALSE);
        int width, height;
        pango_cairo_context_set_resolution(pango_layout_get_context(layout), 300);
        pango_layout_context_changed(layout);
        pango_layout_get_pixel_size(layout, &width, &height);
        assert(height <= 48 && width <= 200);
        g_object_unref(layout);
        cairo_destroy(cr);
    }
    assert(app.mode == previous_mode && app.level == previous_level && app.game.history_len == 1);
    tap_choice(MODE_TWO_PLAYER, FALSE);
    assert(app.pending_mode == MODE_TWO_PLAYER && menu_geometry().height == 289);
    tap_choice(MODE_PLAY_BLACK, FALSE);
    tap_form_button(FALSE);
    assert(app.menu_view == MENU_ACTIONS);
    assert(app.mode == previous_mode && app.level == previous_level && app.game.history_len == 1);
    assert_compact_menu(closed_cell);
    close_menu_at(500, 300);

    assert(save_game());
    assert(g_file_test(app.save_path, G_FILE_TEST_IS_REGULAR));
    open_menu_at(500, 28);
    tap_action(1);
    assert(app.game.history_len == 0 && app.menu_view == MENU_CLOSED);
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
    assert(strstr(current_status(), "保存失败") != NULL);
    open_menu_at(500, 28);
    tap_action(2);
    app.save_path = previous_path;
    g_idle_add(finish_exit_test, NULL);
    return FALSE;
}

static gboolean finish_exit_test(gpointer unused) {
    (void)unused;
    exit_flow_completed = TRUE;
    app.save_protected = TRUE;
    if (app.menu_view != MENU_ACTIONS) show_menu();
    tap_action(0);
    tap_choice(MODE_PLAY_RED, FALSE);
    tap_choice(XIANGQI_AI_EASY, TRUE);
    assert(app.game.history_len == 1 && app.save_protected);
    tap_form_button(TRUE);
    assert(app.mode == MODE_PLAY_RED && app.level == XIANGQI_AI_EASY);
    assert(app.game.history_len == 0 && app.menu_view == MENU_CLOSED);
    assert(!app.save_protected);
    app.mode = MODE_TWO_PLAYER;
    app.level = XIANGQI_AI_HARD;
    tap_square(6, 0);
    tap_square(5, 0);
    assert(app.game.history_len == 1);
    g_remove(app.save_path);
    puts("UI passed: one drawing window, one regional expose per menu toggle, touch controls, legacy save, failed exit kept alive, auto-save");
    open_menu_at(500, 28);
    tap_action(2);
    return FALSE;
}

static gboolean check_resume_message_expired(gpointer unused) {
    (void)unused;
    assert(app.message[0] == '\0');
    assert(strcmp(current_status(), "黑方走棋") == 0);
    assert_board_fixed();
    puts("Resume notice passed: success shown for two seconds, then current turn; board unchanged");
    quit_cb(NULL, NULL);
    return FALSE;
}

static gboolean check_auto_resume(gpointer unused) {
    (void)unused;
    assert(app.game.history_len == 1);
    assert(app.mode == MODE_TWO_PLAYER && app.level == XIANGQI_AI_HARD);
    assert(app.menu_view == MENU_CLOSED);
    assert(strcmp(current_status(), "已恢复上次对局") == 0);
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
    g_setenv("GDK_NATIVE_WINDOWS", "1", TRUE);
    if (argc > 1 && strcmp(argv[1], "--activate") == 0)
        return chess_application_main(argc, argv);
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
    g_timeout_add(300, child_check ? (resume ? check_auto_resume : check_failed_resume) : exercise_ui, NULL);
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
