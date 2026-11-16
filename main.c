/*
 * Exact Chinese Chess
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unofficial Kindle-focused Xiangqi adaptation. Code lineage, artwork
 * provenance, and Pikafish source notes are documented in docs/PROVENANCE.md.
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pikafish_uci.h"
#include "xiangqi_engine.h"
#include "touch_input.h"

#define APP_TITLE "Exact Chinese Chess"
#define KINDLE_WINDOW_TITLE "L:A_N:application_ID:exactchinesechess_PC:N_O:URL"
#define KINDLE_WINDOW_TITLE_TOPBAR "L:A_N:application_PC:T_ID:exactchinesechess_O:URL"
#define LOG_PATH "/mnt/us/exact-chinesechess.log"
#define SAVE_PATH "/mnt/us/extensions/exact-chinesechess/exact-chinesechess.save"
#define LEGACY_SAVE_PATH "/mnt/us/documents/exact-chinesechess.txt"
#define KINDLE_APP_WIDTH 1072
#define KINDLE_APP_HEIGHT 1448
#define DEFAULT_PIKAFISH_PATH "/mnt/us/extensions/exact-chinesechess/bin/armhf/pikafish"

typedef enum {
    MODE_PLAY_RED = 0,
    MODE_PLAY_BLACK,
    MODE_TWO_PLAYER
} AppMode;

typedef enum {
    MENU_CLOSED = 0,
    MENU_ACTIONS,
    MENU_NEW_GAME
} MenuView;

static const char *kindle_window_title(void)
{
    const char *value = g_getenv("KINDLE_SHOW_TOPBAR");
    return (value != NULL && value[0] != '\0' && strcmp(value, "0") != 0) ? KINDLE_WINDOW_TITLE_TOPBAR
                                                                          : KINDLE_WINDOW_TITLE;
}

typedef struct {
    GtkWidget *window;
    GtkWidget *board;
    MenuView menu_view;
    AppMode pending_mode;
    XiangqiAiLevel pending_level;
    XiangqiGame game;
    PikafishUci pikafish;
    GdkPixbuf *board_image;
    GdkPixbuf *piece_images[2][7];
    AppMode mode;
    XiangqiAiLevel level;
    gboolean pikafish_available;
    gboolean save_protected;
    guint ai_source;
    guint message_source;
    char message[192];
    TouchInput touch;
    char *save_path;
    char *log_path;
} AppState;

static AppState app;

static void update_ui(void);
static void maybe_schedule_ai(void);
static void report_history_to_pikafish(void);
static void load_assets(void);
static void clear_assets(void);
static gboolean board_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data);
static gboolean board_button(GtkWidget *widget, GdkEventButton *event, gpointer data);
static GtkAllocation menu_geometry(void);
static void redraw_menu(GtkAllocation old);
static void redraw_status(void);
static void draw_ui_overlay(cairo_t *cr);

static void menu_back_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    GtkAllocation old = menu_geometry();
    app.menu_view = MENU_ACTIONS;
    redraw_menu(old);
}

static void show_menu(void) {
    menu_back_cb(NULL, NULL);
}

static void hide_menu(void) {
    GtkAllocation old = menu_geometry();
    app.menu_view = MENU_CLOSED;
    redraw_menu(old);
}

static void app_log(const char *message) {
    FILE *f = fopen(app.log_path != NULL ? app.log_path : LOG_PATH, "a");
    if (!f) {
        return;
    }
    fprintf(f, "[app] %s\n", message);
    fclose(f);
}

static GdkPixbuf *normalize_piece_pixbuf(GdkPixbuf *src, XiangqiSide side) {
    GdkPixbuf *rgba;
    int width;
    int height;
    int stride;
    int channels;
    int x;
    int y;
    guchar *pixels;
    guchar bg_r;
    guchar bg_g;
    guchar bg_b;
    double cx;
    double cy;
    double radius;

    if (src == NULL) {
        return NULL;
    }

    if (gdk_pixbuf_get_has_alpha(src)) {
        rgba = gdk_pixbuf_copy(src);
    } else {
        const guchar *src_pixels = gdk_pixbuf_get_pixels(src);
        int src_channels = gdk_pixbuf_get_n_channels(src);
        bg_r = src_pixels[0];
        bg_g = src_channels > 1 ? src_pixels[1] : src_pixels[0];
        bg_b = src_channels > 2 ? src_pixels[2] : src_pixels[0];
        rgba = gdk_pixbuf_add_alpha(src, TRUE, bg_r, bg_g, bg_b);
    }

    if (rgba == NULL) {
        return NULL;
    }

    width = gdk_pixbuf_get_width(rgba);
    height = gdk_pixbuf_get_height(rgba);
    stride = gdk_pixbuf_get_rowstride(rgba);
    channels = gdk_pixbuf_get_n_channels(rgba);
    pixels = gdk_pixbuf_get_pixels(rgba);
    bg_r = pixels[0];
    bg_g = pixels[1];
    bg_b = pixels[2];
    cx = (width - 1) / 2.0;
    cy = (height - 1) / 2.0;
    radius = MIN(width, height) * 0.49;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            guchar *p = pixels + y * stride + x * channels;
            int dr = (int)p[0] - (int)bg_r;
            int dg = (int)p[1] - (int)bg_g;
            int db = (int)p[2] - (int)bg_b;
            double dx = x - cx;
            double dy = y - cy;
            gboolean outside_piece = dx * dx + dy * dy > radius * radius;
            gboolean background_color = dr * dr + dg * dg + db * db < 64;

            if (outside_piece || background_color) {
                p[3] = 0;
                continue;
            }

            if (side == XIANGQI_BLACK && p[3] != 0 &&
                p[0] > 178 && p[1] > 178 && p[2] > 178) {
                p[0] = 165;
                p[1] = 165;
                p[2] = 165;
            }

            if (p[3] != 0 &&
                p[0] > 160 && p[0] > p[1] + 12 && p[0] > p[2] + 12) {
                p[0] = side == XIANGQI_RED ? 70 : 0;
                p[1] = p[0];
                p[2] = p[0];
            }
        }
    }

    return rgba;
}

static gboolean clear_message(gpointer data) {
    (void)data;
    app.message_source = 0;
    app.message[0] = '\0';
    redraw_status();
    return FALSE;
}

static void set_message(const char *message) {
    if (app.message_source != 0) {
        g_source_remove(app.message_source);
        app.message_source = 0;
    }
    g_strlcpy(app.message, message, sizeof(app.message));
    if (app.board == NULL) return;
    redraw_status();
    if (message[0] != '\0') {
        app.message_source = g_timeout_add(2000, clear_message, NULL);
    }
}

static gboolean is_ai_turn(void) {
    if (app.game.game_over) {
        return FALSE;
    }
    switch (app.mode) {
    case MODE_PLAY_RED:
        return app.game.turn == XIANGQI_BLACK;
    case MODE_PLAY_BLACK:
        return app.game.turn == XIANGQI_RED;
    case MODE_TWO_PLAYER:
        return FALSE;
    }
    return FALSE;
}

static gboolean is_human_turn(void) {
    if (app.game.game_over) {
        return FALSE;
    }
    switch (app.mode) {
    case MODE_PLAY_RED:
        return app.game.turn == XIANGQI_RED;
    case MODE_PLAY_BLACK:
        return app.game.turn == XIANGQI_BLACK;
    case MODE_TWO_PLAYER:
        return TRUE;
    }
    return FALSE;
}

static const char *piece_ascii_label(const XiangqiPiece *piece) {
    static const char *red[] = { "K", "A", "E", "H", "R", "C", "P" };
    static const char *black[] = { "k", "a", "e", "h", "r", "c", "p" };
    return piece->side == XIANGQI_RED ? red[piece->type] : black[piece->type];
}

static void move_to_uci(const XiangqiMove *move, char *buf, int len) {
    if (len < 5) {
        return;
    }
    buf[0] = (char)('a' + move->from_col);
    buf[1] = (char)('0' + (9 - move->from_row));
    buf[2] = (char)('a' + move->to_col);
    buf[3] = (char)('0' + (9 - move->to_row));
    buf[4] = '\0';
}

static gboolean uci_to_move(const char *uci, int *move_id, int *row, int *col) {
    int from_col;
    int from_row;
    int to_col;
    int to_row;

    if (uci == NULL || strlen(uci) < 4) {
        return FALSE;
    }
    from_col = uci[0] - 'a';
    from_row = 9 - (uci[1] - '0');
    to_col = uci[2] - 'a';
    to_row = 9 - (uci[3] - '0');
    if (from_col < 0 || from_col >= XIANGQI_COLS ||
        to_col < 0 || to_col >= XIANGQI_COLS ||
        from_row < 0 || from_row >= XIANGQI_ROWS ||
        to_row < 0 || to_row >= XIANGQI_ROWS) {
        return FALSE;
    }
    *move_id = xiangqi_piece_at(&app.game, from_row, from_col);
    *row = to_row;
    *col = to_col;
    return *move_id >= 0;
}

static void app_apply_high_contrast(GtkWidget *widget) {
    GdkColor black = { 0, 0x0000, 0x0000, 0x0000 };
    GdkColor white = { 0, 0xffff, 0xffff, 0xffff };

    gtk_widget_modify_fg(widget, GTK_STATE_NORMAL, &black);
    gtk_widget_modify_fg(widget, GTK_STATE_ACTIVE, &black);
    gtk_widget_modify_fg(widget, GTK_STATE_SELECTED, &white);
    gtk_widget_modify_text(widget, GTK_STATE_NORMAL, &black);
    gtk_widget_modify_text(widget, GTK_STATE_SELECTED, &white);
    gtk_widget_modify_base(widget, GTK_STATE_NORMAL, &white);
    gtk_widget_modify_base(widget, GTK_STATE_SELECTED, &black);
    gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &white);
    gtk_widget_modify_bg(widget, GTK_STATE_SELECTED, &black);
}

static gboolean ai_move_cb(gpointer data) {
    XiangqiMove move;
    (void)data;

    app.ai_source = 0;
    if (!is_ai_turn()) {
        return FALSE;
    }
    /* Mark engine unavailable if it died before we could use it. */
    if (app.pikafish_available && app.pikafish.dead)
        app.pikafish_available = FALSE;
    if (app.pikafish_available && !app.pikafish.ready) {
        app.ai_source = g_timeout_add(250, ai_move_cb, NULL);
        return FALSE;
    }
    if (app.pikafish_available && pikafish_uci_request_move(&app.pikafish)) {
        return FALSE;
    }
    if (xiangqi_choose_ai_move(&app.game, app.level, &move) &&
        xiangqi_apply_move(&app.game, move.move_id, move.to_row, move.to_col, &move)) {
        char uci[8];
        move_to_uci(&move, uci, sizeof(uci));
        pikafish_uci_report_move(&app.pikafish, uci);
    } else {
        app.game.game_over = true;
        app.game.winner = app.game.turn == XIANGQI_RED ? XIANGQI_BLACK : XIANGQI_RED;
    }
    update_ui();
    maybe_schedule_ai();
    return FALSE;
}

static void pikafish_bestmove_cb(const char *uci, gpointer user_data) {
    int move_id;
    int row;
    int col;
    XiangqiMove move;
    (void)user_data;

    if (!is_ai_turn()) {
        return;
    }

    if (uci_to_move(uci, &move_id, &row, &col) &&
        xiangqi_apply_move(&app.game, move_id, row, col, &move)) {
        char applied_uci[8];
        move_to_uci(&move, applied_uci, sizeof(applied_uci));
        pikafish_uci_report_move(&app.pikafish, applied_uci);
    } else {
        app.pikafish_available = FALSE;
        if (xiangqi_choose_ai_move(&app.game, app.level, &move) &&
            xiangqi_apply_move(&app.game, move.move_id, move.to_row, move.to_col, &move)) {
            char fallback_uci[8];
            move_to_uci(&move, fallback_uci, sizeof(fallback_uci));
            pikafish_uci_report_move(&app.pikafish, fallback_uci);
        } else {
            app.game.game_over = true;
            app.game.winner = app.game.turn == XIANGQI_RED ? XIANGQI_BLACK : XIANGQI_RED;
        }
    }
    update_ui();
    maybe_schedule_ai();
}

static void maybe_schedule_ai(void) {
    if (app.ai_source != 0) {
        g_source_remove(app.ai_source);
        app.ai_source = 0;
    }
    if (is_ai_turn()) {
        app.ai_source = g_timeout_add(250, ai_move_cb, NULL);
    }
}

static void new_game(void) {
    if (app.ai_source != 0) {
        g_source_remove(app.ai_source);
        app.ai_source = 0;
    }
    xiangqi_init(&app.game);
    app.save_protected = FALSE;
    pikafish_uci_start_game(&app.pikafish);
    set_message("");
    update_ui();
    maybe_schedule_ai();
}

static void new_game_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    app.mode = app.pending_mode;
    app.level = app.pending_level;
    pikafish_uci_set_difficulty(&app.pikafish, app.level);
    new_game();
    hide_menu();
}

static void show_new_game_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    GtkAllocation old = menu_geometry();
    app.pending_mode = app.mode;
    app.pending_level = app.level;
    app.menu_view = MENU_NEW_GAME;
    redraw_menu(old);
}

static void undo_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    if (app.ai_source != 0) {
        g_source_remove(app.ai_source);
        app.ai_source = 0;
    }
    if (xiangqi_undo(&app.game)) {
        if (app.mode == MODE_PLAY_RED || app.mode == MODE_PLAY_BLACK) {
            xiangqi_undo(&app.game);
        }
        set_message("已悔棋");
        report_history_to_pikafish();
        update_ui();
    }
}

static gboolean save_game(void) {
    GString *contents = g_string_new(NULL);
    GError *error = NULL;
    gboolean saved;
    int i;
    g_string_append_printf(contents, "exact-chinesechess 1\n%d\n", app.game.history_len);
    for (i = 0; i < app.game.history_len; i++) {
        XiangqiMove *m = &app.game.history[i];
        g_string_append_printf(contents, "%d %d\n", m->move_id, m->to_row * 9 + m->to_col);
    }
    g_string_append_printf(contents, "settings %d %d\n", app.mode, app.level);
    saved = g_file_set_contents(app.save_path, contents->str, contents->len, &error);
    g_string_free(contents, TRUE);
    if (!saved) {
        app_log(error->message);
        g_error_free(error);
        set_message("保存失败，请检查存储空间后重试退出");
    }
    update_ui();
    return saved;
}

static gboolean load_game(void) {
    FILE *f;
    char header[64];
    char footer[32];
    XiangqiGame restored;
    int version;
    int count;
    int i;
    int mode = app.mode;
    int level = app.level;

    f = fopen(app.save_path, "r");
    if (f == NULL && strcmp(app.save_path, SAVE_PATH) != 0)
        f = fopen(SAVE_PATH, "r");
    if (f == NULL)
        f = fopen(LEGACY_SAVE_PATH, "r");
    if (!f) {
        set_message("还没有存档");
        update_ui();
        return FALSE;
    }
    if (fscanf(f, "%63s %d\n%d\n", header, &version, &count) != 3 ||
        strcmp(header, "exact-chinesechess") != 0 || version != 1 ||
        count < 0 || count > XIANGQI_MAX_HISTORY) {
        fclose(f);
        set_message("存档格式无效");
        update_ui();
        return FALSE;
    }
    xiangqi_init(&restored);
    for (i = 0; i < count; i++) {
        int id;
        int square;
        XiangqiMove move;
        if (fscanf(f, "%d %d\n", &id, &square) != 2 ||
            !xiangqi_apply_move(&restored, id, square / 9, square % 9, &move)) {
            fclose(f);
            set_message("存档无法还原");
            update_ui();
            return FALSE;
        }
    }
    if (fscanf(f, " %31s", footer) == 1 && strcmp(footer, "settings") == 0) {
        if (fscanf(f, "%d %d", &mode, &level) != 2 ||
              mode < MODE_PLAY_RED || mode > 3 ||
            level < XIANGQI_AI_EASY || level > XIANGQI_AI_HARD) {
            fclose(f);
            set_message("存档设置无效");
            update_ui();
            return FALSE;
        }
    }
    fclose(f);
    /* Retain positions saved by the removed automatic demonstration mode. */
    if (mode == 3) mode = MODE_PLAY_RED;
    app.game = restored;
    app.mode = (AppMode)mode;
    app.level = (XiangqiAiLevel)level;
    app.save_protected = FALSE;
    pikafish_uci_set_difficulty(&app.pikafish, app.level);
    set_message("已恢复上次对局");
    report_history_to_pikafish();
    update_ui();
    maybe_schedule_ai();
    return TRUE;
}

static gboolean quit_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    if (!app.save_protected && !save_game()) return TRUE;
    gtk_main_quit();
    return TRUE;
}

static void report_history_to_pikafish(void) {
    int i;

    pikafish_uci_start_game(&app.pikafish);
    for (i = 0; i < app.game.history_len; i++) {
        char uci[8];
        move_to_uci(&app.game.history[i], uci, sizeof(uci));
        pikafish_uci_report_move(&app.pikafish, uci);
    }
}

static void load_assets(void) {
    int side;
    int type;
    GError *error = NULL;
    const char *prefix = "assets/xiangqi";
    static const char *piece_names[] = { "k", "a", "b", "n", "r", "c", "p" };

    app.board_image = gdk_pixbuf_new_from_file("assets/xiangqi/board.png", &error);
    if (app.board_image == NULL && error != NULL) {
        app_log(error->message);
        g_error_free(error);
        error = NULL;
    }

    for (side = 0; side < 2; side++) {
        for (type = 0; type < 7; type++) {
            char path[128];
            char artwork_side = (side == XIANGQI_RED || type == XIANGQI_CANNON) ? 'r' : 'b';
            snprintf(path, sizeof(path), "%s/%c_%s.png", prefix, artwork_side, piece_names[type]);
            GdkPixbuf *loaded = gdk_pixbuf_new_from_file(path, &error);
            if (loaded != NULL) {
                app.piece_images[side][type] = normalize_piece_pixbuf(loaded, side);
                g_object_unref(loaded);
            }
            if (app.piece_images[side][type] == NULL && error != NULL) {
                app_log(error->message);
                g_error_free(error);
                error = NULL;
            }
        }
    }
}

static void clear_assets(void) {
    int side;
    int type;

    if (app.board_image != NULL) {
        g_object_unref(app.board_image);
        app.board_image = NULL;
    }
    for (side = 0; side < 2; side++) {
        for (type = 0; type < 7; type++) {
            if (app.piece_images[side][type] != NULL) {
                g_object_unref(app.piece_images[side][type]);
                app.piece_images[side][type] = NULL;
            }
        }
    }
}

static void board_geometry(GtkWidget *widget, double *left, double *top, double *cell) {
    GtkAllocation a;
    double board_w;
    double board_h;
    double usable_w;
    double usable_h;

    gtk_widget_get_allocation(widget, &a);
    /* 留出棋子半径，避免边缘棋子被裁切。 */
    usable_w = fmax(1, a.width - 32);
    usable_h = fmax(1, a.height - 32);
    *cell = fmin(usable_w / 8.9, usable_h / 9.9);
    board_w = *cell * 8.0;
    board_h = *cell * 9.0;
    *left = (a.width - board_w) / 2.0;
    *top = (a.height - board_h) / 2.0;
}

static void draw_centered_text(cairo_t *cr, const char *text, double x, double y, double size, gboolean bold) {
    cairo_text_extents_t extents;

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, x - extents.width / 2.0 - extents.x_bearing, y - extents.height / 2.0 - extents.y_bearing);
    cairo_show_text(cr, text);
}

static void draw_star(cairo_t *cr, double x, double y, double s) {
    cairo_move_to(cr, x - s, y);
    cairo_line_to(cr, x + s, y);
    cairo_move_to(cr, x, y - s);
    cairo_line_to(cr, x, y + s);
}

static void draw_cell_highlight(cairo_t *cr, double left, double top, double cell, int row, int col, gboolean selected) {
    double x = left + col * cell;
    double y = top + row * cell;
    double r = cell * 0.38;

    if (row < 0 || row >= XIANGQI_ROWS || col < 0 || col >= XIANGQI_COLS) {
        return;
    }

    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * G_PI);
    if (selected) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.26);
    } else {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.14);
    }
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, selected ? 0.95 : 0.65);
    cairo_set_line_width(cr, selected ? 4.0 : 2.6);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_cell_focus_ring(cairo_t *cr, double left, double top, double cell, int row, int col, gboolean current) {
    double x = left + col * cell;
    double y = top + row * cell;
    double r = cell * (current ? 0.49 : 0.44);

    if (row < 0 || row >= XIANGQI_ROWS || col < 0 || col >= XIANGQI_COLS) {
        return;
    }

    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, current ? 0.0 : 1.0, current ? 0.0 : 1.0, current ? 0.0 : 1.0);
    cairo_set_line_width(cr, current ? cell * 0.075 : cell * 0.045);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, current ? 1.0 : 0.0, current ? 1.0 : 0.0, current ? 1.0 : 0.0);
    cairo_set_line_width(cr, current ? cell * 0.025 : cell * 0.02);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static gboolean board_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr;
    XiangqiGame view;
    double left;
    double top;
    double cell;
    int row;
    int col;
    int i;
    int ply;
    (void)data;

    cr = gdk_cairo_create(widget->window);
    if (event->region) {
        gdk_cairo_region(cr, event->region);
        cairo_clip(cr);
    }
    ply = app.game.history_len;
    view = app.game;
    board_geometry(widget, &left, &top, &cell);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    if (app.board_image != NULL) {
        double image_left = left - cell * 0.5;
        double image_top = top - cell * 0.5;
        double scale = (cell * 9.0) / gdk_pixbuf_get_width(app.board_image);

        cairo_save(cr);
        cairo_translate(cr, image_left, image_top);
        cairo_scale(cr, scale, scale);
        gdk_cairo_set_source_pixbuf(cr, app.board_image, 0, 0);
        cairo_rectangle(cr, 0, 0, gdk_pixbuf_get_width(app.board_image), gdk_pixbuf_get_height(app.board_image));
        cairo_fill(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, 2.0);

        cairo_new_path(cr);
        for (row = 0; row < XIANGQI_ROWS; row++) {
            double y = top + row * cell;
            cairo_move_to(cr, left, y);
            cairo_line_to(cr, left + 8 * cell, y);
        }
        for (col = 0; col < XIANGQI_COLS; col++) {
            double x = left + col * cell;
            cairo_move_to(cr, x, top);
            cairo_line_to(cr, x, top + 4 * cell);
            cairo_move_to(cr, x, top + 5 * cell);
            cairo_line_to(cr, x, top + 9 * cell);
        }
        cairo_rectangle(cr, left, top, 8 * cell, 9 * cell);
        cairo_move_to(cr, left + 3 * cell, top);
        cairo_line_to(cr, left + 5 * cell, top + 2 * cell);
        cairo_move_to(cr, left + 5 * cell, top);
        cairo_line_to(cr, left + 3 * cell, top + 2 * cell);
        cairo_move_to(cr, left + 3 * cell, top + 7 * cell);
        cairo_line_to(cr, left + 5 * cell, top + 9 * cell);
        cairo_move_to(cr, left + 5 * cell, top + 7 * cell);
        cairo_line_to(cr, left + 3 * cell, top + 9 * cell);
        cairo_stroke(cr);

        cairo_new_path(cr);
        cairo_set_line_width(cr, 1.5);
        draw_star(cr, left + 1 * cell, top + 2 * cell, cell * 0.08);
        draw_star(cr, left + 7 * cell, top + 2 * cell, cell * 0.08);
        for (col = 0; col <= 8; col += 2) {
            draw_star(cr, left + col * cell, top + 3 * cell, cell * 0.08);
            draw_star(cr, left + col * cell, top + 6 * cell, cell * 0.08);
        }
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0, 0, 0);
        draw_centered_text(cr, "RIVER", left + 2.5 * cell, top + 4.5 * cell, cell * 0.22, TRUE);
        draw_centered_text(cr, "BORDER", left + 5.5 * cell, top + 4.5 * cell, cell * 0.22, TRUE);
    }

    if (ply > 0) {
        XiangqiMove last = app.game.history[ply - 1];
        draw_cell_highlight(cr, left, top, cell, last.from_row, last.from_col, FALSE);
        draw_cell_highlight(cr, left, top, cell, last.to_row, last.to_col, FALSE);
    }
    if (view.selected_id >= 0 && view.selected_id < XIANGQI_PIECES && !view.pieces[view.selected_id].dead) {
        XiangqiPiece *selected = &view.pieces[view.selected_id];
        draw_cell_highlight(cr, left, top, cell, selected->row, selected->col, TRUE);
    }

    for (i = 0; i < XIANGQI_PIECES; i++) {
        XiangqiPiece *p = &view.pieces[i];
        double x;
        double y;
        double r;

        if (p->dead) {
            continue;
        }
        x = left + p->col * cell;
        y = top + p->row * cell;
        r = cell * 0.38;

        if (app.piece_images[p->side][p->type] != NULL) {
            double size = cell * 0.84;
            double img_w = gdk_pixbuf_get_width(app.piece_images[p->side][p->type]);
            double scale = size / img_w;

            cairo_save(cr);
            cairo_new_path(cr);
            cairo_arc(cr, x, y, size * 0.48, 0, 2 * G_PI);
            if (p->side == XIANGQI_BLACK) {
                cairo_set_source_rgb(cr, 0.64, 0.64, 0.64);
            } else {
                cairo_set_source_rgb(cr, 1, 1, 1);
            }
            cairo_fill_preserve(cr);
            cairo_clip(cr);
            cairo_translate(cr, x - size * 0.5, y - size * 0.5);
            cairo_scale(cr, scale, scale);
            gdk_cairo_set_source_pixbuf(cr, app.piece_images[p->side][p->type], 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            /* Red pieces use red ink which e-ink renders as light grey — darken
             * with a semi-transparent black ring so they stay legible. */
            if (p->side == XIANGQI_RED) {
                cairo_new_path(cr);
                cairo_arc(cr, x, y, size * 0.48, 0, 2 * G_PI);
                cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
                cairo_set_line_width(cr, size * 0.06);
                cairo_stroke(cr);
            }
        } else {
            cairo_new_path(cr);
            cairo_arc(cr, x, y, r, 0, 2 * G_PI);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_fill_preserve(cr);
            if (view.selected_id == i || p->side == XIANGQI_RED) {
                cairo_set_source_rgb(cr, 0, 0, 0);
                cairo_set_line_width(cr, view.selected_id == i ? 5.0 : 2.0);
                cairo_stroke(cr);
            } else {
                cairo_new_path(cr);
            }

            if (p->side == XIANGQI_RED) {
                cairo_new_path(cr);
                cairo_arc(cr, x, y, r * 0.78, 0, 2 * G_PI);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
            }

            cairo_set_source_rgb(cr, 0, 0, 0);
            draw_centered_text(cr, piece_ascii_label(p), x, y, cell * 0.42, TRUE);
        }

        if (view.selected_id == i && app.piece_images[p->side][p->type] != NULL) {
            cairo_new_path(cr);
            cairo_arc(cr, x, y, cell * 0.42, 0, 2 * G_PI);
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_set_line_width(cr, 4.0);
            cairo_stroke(cr);
        }
    }

    if (ply > 0) {
        XiangqiMove last = app.game.history[ply - 1];
        draw_cell_focus_ring(cr, left, top, cell, last.from_row, last.from_col, FALSE);
        draw_cell_focus_ring(cr, left, top, cell, last.to_row, last.to_col, TRUE);
    }
    if (view.selected_id >= 0 && view.selected_id < XIANGQI_PIECES && !view.pieces[view.selected_id].dead) {
        XiangqiPiece *selected = &view.pieces[view.selected_id];
        draw_cell_focus_ring(cr, left, top, cell, selected->row, selected->col, TRUE);
    }

    draw_ui_overlay(cr);
    cairo_destroy(cr);
    return FALSE;
}

static gboolean board_tap(GtkWidget *widget, GdkEventButton *event) {
    double left;
    double top;
    double cell;
    int row;
    int col;
    int id;

    if (event->button != 1) return TRUE;

    board_geometry(widget, &left, &top, &cell);
    col = (int)floor((event->x - left + cell / 2.0) / cell);
    row = (int)floor((event->y - top + cell / 2.0) / cell);
    if (row < 0 || row >= XIANGQI_ROWS) {
        show_menu();
        return TRUE;
    }

    if (!is_human_turn()) {
        return TRUE;
    }

    if (row < 0 || row >= XIANGQI_ROWS || col < 0 || col >= XIANGQI_COLS) {
        return TRUE;
    }

    id = xiangqi_piece_at(&app.game, row, col);
    if (app.game.selected_id < 0) {
        if (id != -1 && app.game.pieces[id].side == app.game.turn) {
            app.game.selected_id = id;
        }
    } else {
        XiangqiMove move;
        if (id != -1 && app.game.pieces[id].side == app.game.turn) {
            app.game.selected_id = id;
        } else if (xiangqi_apply_move(&app.game, app.game.selected_id, row, col, &move)) {
            char uci[8];
            move_to_uci(&move, uci, sizeof(uci));
            pikafish_uci_report_move(&app.pikafish, uci);
            app.game.selected_id = -1;
            maybe_schedule_ai();
        } else {
            set_message("此处不能落子");
        }
    }
    update_ui();
    return TRUE;
}

static void touch_coordinates(GtkWidget *widget, double x, double y, double *window_x, double *window_y) {
    gint offset_x = 0, offset_y = 0;
    gtk_widget_translate_coordinates(widget, app.window, 0, 0, &offset_x, &offset_y);
    *window_x = x + offset_x;
    *window_y = y + offset_y;
}

static void show_system_menu(void) {
    gchar *argv[] = { "lipc-set-prop", "com.lab126.kppchromebar", "configurePillState", "openedBySwipe", NULL };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        app_log(error->message);
        g_error_free(error);
        set_message("系统菜单打开失败");
        update_ui();
    }
}

static gboolean board_button(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    GtkAllocation size;
    double x, y;
    (void)data;
    if (event->button != 1) return FALSE;
    gtk_widget_get_allocation(app.window, &size);
    touch_coordinates(widget, event->x, event->y, &x, &y);
    touch_input_begin(&app.touch, x, y, size.height);
    return TRUE;
}

static gboolean board_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    double x, y;
    (void)data;
    touch_coordinates(widget, event->x, event->y, &x, &y);
    touch_input_motion(&app.touch, x, y);
    return TRUE;
}

static const char *current_status(void) {
    if (app.message[0]) return app.message;
    if (app.game.game_over)
        return app.game.winner == XIANGQI_RED ? "红方获胜" : "黑方获胜";
    return app.game.turn == XIANGQI_RED ? "红方走棋" : "黑方走棋";
}

static void redraw_status(void) {
    GtkAllocation size;
    if (!app.board) return;
    gtk_widget_get_allocation(app.board, &size);
    gtk_widget_queue_draw_area(app.board, 24, 24, MAX(1, size.width - 48), 56);
}

static void update_ui(void) {
    if (app.board) gtk_widget_queue_draw(app.board);
}

static GtkAllocation menu_geometry(void) {
    GtkAllocation size = { 0, 0, KINDLE_APP_WIDTH, KINDLE_APP_HEIGHT };
    GtkAllocation panel;
    if (app.board) gtk_widget_get_allocation(app.board, &size);
    panel.x = 24;
    panel.width = MAX(1, size.width - 48);
    panel.height = app.menu_view == MENU_CLOSED ? 0 :
                   app.menu_view == MENU_ACTIONS ? 148 :
                   app.pending_mode == MODE_TWO_PLAYER ? 289 : 430;
    panel.y = MAX(24, size.height - panel.height - 24);
    return panel;
}

static void redraw_menu(GtkAllocation old) {
    GtkAllocation next = menu_geometry();
    int x, y, right, bottom;
    if (!app.board || (!old.height && !next.height)) return;
    if (!old.height) old = next;
    if (!next.height) next = old;
    x = MIN(old.x, next.x);
    y = MIN(old.y, next.y);
    right = MAX(old.x + old.width, next.x + next.width);
    bottom = MAX(old.y + old.height, next.y + next.height);
    gtk_widget_queue_draw_area(app.board, x, y, right - x, bottom - y);
}

static PangoLayout *ui_text_layout(cairo_t *cr, const char *text, gboolean bold) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_family(font, "Sans");
    pango_font_description_set_absolute_size(font, 32 * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    return layout;
}

static void draw_ui_text(cairo_t *cr, const char *text, double x, double y,
                         double width, double height, gboolean bold, gboolean centered) {
    PangoLayout *layout = ui_text_layout(cr, text, bold);
    int text_width, text_height;
    pango_layout_set_width(layout, MAX(1, (int)width) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, centered ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    pango_layout_get_pixel_size(layout, &text_width, &text_height);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_move_to(cr, x, y + (height - text_height) / 2.0);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

static void draw_choice_row(cairo_t *cr, GtkAllocation panel, int row,
                            const char **labels, int selected) {
    int inner = panel.width - 32;
    int column = MAX(1, (inner - 24) / 3);
    int i;
    for (i = 0; i < 3; i++) {
        int x = panel.x + 16 + i * (column + 12);
        int width = i == 2 ? panel.x + panel.width - 16 - x : column;
        double y = panel.y + row + 42;
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_line_width(cr, 1.5);
        cairo_new_path(cr);
        cairo_arc(cr, x + 24, y, 11, 0, 2 * G_PI);
        cairo_stroke(cr);
        if (i == selected) {
            cairo_arc(cr, x + 24, y, 5, 0, 2 * G_PI);
            cairo_fill(cr);
        }
        draw_ui_text(cr, labels[i], x + 64, panel.y + row,
                     width - 64, 84, FALSE, FALSE);
    }
}

/* Status, handle, buttons and board share one buffered drawing window. */
static void draw_ui_overlay(cairo_t *cr) {
    const char *actions[] = { "新局", "悔棋", "退出" };
    const char *modes[] = { "我执红", "我执黑", "双人对弈" };
    const char *levels[] = { "简单", "中等", "困难" };
    GtkAllocation size, panel = menu_geometry();
    int inner = panel.width - 32;
    int i;
    gtk_widget_get_allocation(app.board, &size);
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 24, 24, MAX(1, size.width - 48), 56);
    cairo_fill(cr);
    draw_ui_text(cr, current_status(), 24, 24, size.width - 48, 56, FALSE, TRUE);
    if (app.menu_view == MENU_CLOSED) {
        cairo_restore(cr);
        return;
    }
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, panel.x, panel.y, panel.width, panel.height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, panel.x + 0.5, panel.y + 0.5, panel.width - 1, panel.height - 1);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, panel.x + panel.width / 2.0 - 24, panel.y + 30);
    cairo_line_to(cr, panel.x + panel.width / 2.0 + 24, panel.y + 30);
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    if (app.menu_view == MENU_ACTIONS) {
        int column = MAX(1, inner / 3);
        for (i = 0; i < 3; i++) {
            int x = panel.x + 16 + i * column;
            int width = i == 2 ? panel.x + panel.width - 16 - x : column;
            draw_ui_text(cr, actions[i], x, panel.y + 52, width, 80, FALSE, TRUE);
            if (i < 2) {
                cairo_set_line_width(cr, 1);
                cairo_move_to(cr, x + width - 0.5, panel.y + 68);
                cairo_line_to(cr, x + width - 0.5, panel.y + 116);
                cairo_stroke(cr);
            }
        }
    } else {
        int buttons_y = app.pending_mode == MODE_TWO_PLAYER ? 193 : 334;
        int button_width = MAX(1, (inner - 12) / 2);
        draw_ui_text(cr, "对弈方式", panel.x + 16, panel.y + 52, inner, 33, TRUE, FALSE);
        draw_choice_row(cr, panel, 97, modes, app.pending_mode);
        if (app.pending_mode != MODE_TWO_PLAYER) {
            draw_ui_text(cr, "电脑难度", panel.x + 16, panel.y + 193, inner, 33, TRUE, FALSE);
            draw_choice_row(cr, panel, 238, levels, app.pending_level);
        }
        draw_ui_text(cr, "取消", panel.x + 16, panel.y + buttons_y,
                     button_width, 80, FALSE, TRUE);
        draw_ui_text(cr, "开始新局", panel.x + 28 + button_width, panel.y + buttons_y,
                     inner - button_width - 12, 80, FALSE, TRUE);
    }
    cairo_restore(cr);
}

static int choice_at(double x, int width) {
    int column = MAX(1, (width - 32 - 24) / 3);
    int index;
    if (x < 16 || x >= width - 16) return -1;
    index = MIN(2, (int)(x - 16) / (column + 12));
    if (index < 2 && x >= 16 + index * (column + 12) + column) return -1;
    return index;
}

static void menu_tap(double x, double y) {
    GtkAllocation panel = menu_geometry();
    double local_x = x - panel.x, local_y = y - panel.y;
    int index;
    if (local_x < 0 || local_x >= panel.width || local_y < 0 ||
        local_y >= panel.height || local_y < 52) {
        hide_menu();
        return;
    }
    if (app.menu_view == MENU_ACTIONS) {
        if (local_y >= 132 || local_x < 16 || local_x >= panel.width - 16) {
            hide_menu();
            return;
        }
        index = MIN(2, (int)(local_x - 16) / MAX(1, (panel.width - 32) / 3));
        if (index == 0) show_new_game_cb(NULL, NULL);
        else if (index == 1) {
            undo_cb(NULL, NULL);
            hide_menu();
        } else quit_cb(NULL, NULL);
        return;
    }
    index = choice_at(local_x, panel.width);
    if (index >= 0 && local_y >= 97 && local_y < 181) {
        app.pending_mode = (AppMode)index;
        redraw_menu(panel);
    } else if (index >= 0 && app.pending_mode != MODE_TWO_PLAYER &&
               local_y >= 238 && local_y < 322) {
        app.pending_level = (XiangqiAiLevel)index;
        redraw_menu(panel);
    } else {
        int buttons_y = app.pending_mode == MODE_TWO_PLAYER ? 193 : 334;
        int button_width = MAX(1, (panel.width - 44) / 2);
        if (local_y < buttons_y || local_y >= buttons_y + 80) return;
        if (local_x >= 16 && local_x < 16 + button_width) menu_back_cb(NULL, NULL);
        else if (local_x >= 28 + button_width && local_x < panel.width - 16)
            new_game_cb(NULL, NULL);
    }
}

static gboolean board_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    gboolean open = app.menu_view != MENU_CLOSED;
    TouchAction action;
    double x, y;
    (void)data;
    if (event->button != 1) return FALSE;
    touch_coordinates(widget, event->x, event->y, &x, &y);
    action = touch_input_end(&app.touch, x, y, open);
    if (action == TOUCH_OPEN_SYSTEM_MENU) show_system_menu();
    else if (action == TOUCH_OPEN_MENU) show_menu();
    else if (action == TOUCH_CLOSE_MENU) hide_menu();
    else if (action == TOUCH_TAP && open) menu_tap(x, y);
    else if (action == TOUCH_TAP && widget == app.board) return board_tap(widget, event);
    return TRUE;
}

static void connect_touch(GtkWidget *widget) {
    gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                  GDK_POINTER_MOTION_MASK);
    g_signal_connect(widget, "button-press-event", G_CALLBACK(board_button), NULL);
    g_signal_connect(widget, "motion-notify-event", G_CALLBACK(board_motion), NULL);
    g_signal_connect(widget, "button-release-event", G_CALLBACK(board_release), NULL);
}

static gboolean prepare_data_paths(void) {
    const char *directory = g_getenv("EXACT_CHINESECHESS_DATA_DIR");
    if (directory != NULL && directory[0] != '\0') {
        if (g_mkdir_with_parents(directory, 0700) != 0) {
            g_printerr("Cannot create data directory: %s\n", directory);
            return FALSE;
        }
        app.save_path = g_build_filename(directory, "game.save", NULL);
        app.log_path = g_build_filename(directory, "app.log", NULL);
    } else {
        app.save_path = g_strdup(SAVE_PATH);
        app.log_path = g_strdup(LOG_PATH);
    }
    return TRUE;
}

static gboolean activate_existing_window(void) {
    Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window root, parent, *windows = NULL;
    unsigned int count = 0, i;
    gboolean activated = FALSE;
    gdk_error_trap_push();
    if (XQueryTree(display, DefaultRootWindow(display), &root, &parent, &windows, &count)) {
        for (i = 0; i < count; i++) {
            char *title = NULL;
            if (XFetchName(display, windows[i], &title)) {
                if (title && strstr(title, "ID:exactchinesechess_") != NULL) {
                    XMapRaised(display, windows[i]);
                    XSetInputFocus(display, windows[i], RevertToParent, CurrentTime);
                    activated = TRUE;
                }
                XFree(title);
            }
            if (activated) break;
        }
    }
    if (windows) XFree(windows);
    XSync(display, False);
    if (gdk_error_trap_pop() != 0) activated = FALSE;
    return activated;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    if (argc > 1 && strcmp(argv[1], "--activate") == 0)
        return activate_existing_window() ? 0 : 1;
    if (!prepare_data_paths()) return 1;
    app.mode = MODE_PLAY_RED;
    app.level = XIANGQI_AI_MEDIUM;
    xiangqi_init(&app.game);
    pikafish_uci_init(&app.pikafish,
                      g_getenv("EXACT_CHINESECHESS_PIKAFISH") != NULL ?
                      g_getenv("EXACT_CHINESECHESS_PIKAFISH") : DEFAULT_PIKAFISH_PATH);
    pikafish_uci_set_move_callback(&app.pikafish, pikafish_bestmove_cb, NULL);
    load_assets();
    set_message("");

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app_apply_high_contrast(app.window);
    gtk_window_set_title(GTK_WINDOW(app.window), kindle_window_title());
    gtk_window_set_default_size(GTK_WINDOW(app.window), KINDLE_APP_WIDTH, KINDLE_APP_HEIGHT);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 0);
    connect_touch(app.window);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(quit_cb), NULL);
    app.board = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.board, 240, 240);
    gtk_widget_set_double_buffered(app.board, TRUE);
    gtk_container_add(GTK_CONTAINER(app.window), app.board);
    g_signal_connect(app.board, "expose-event", G_CALLBACK(board_expose), NULL);
    connect_touch(app.board);

    app_log("starting exact-chinesechess");
    {
        GError *error = NULL;
        app.pikafish_available = pikafish_uci_start(&app.pikafish, &error);
        if (error != NULL) {
            app_log(error->message);
            g_error_free(error);
        }
    }
    if (g_file_test(app.save_path, G_FILE_TEST_IS_REGULAR) ||
        g_file_test(SAVE_PATH, G_FILE_TEST_IS_REGULAR) ||
        g_file_test(LEGACY_SAVE_PATH, G_FILE_TEST_IS_REGULAR)) {
        app.save_protected = !load_game();
        if (app.save_protected)
            set_message("存档读取失败，原文件已保留；请开始新局");
    }
    update_ui();
    gtk_widget_show_all(app.window);
    gtk_window_present(GTK_WINDOW(app.window));
    maybe_schedule_ai();
    gtk_main();
    if (app.message_source != 0) g_source_remove(app.message_source);
    pikafish_uci_clear(&app.pikafish);
    clear_assets();
    g_free(app.save_path);
    g_free(app.log_path);
    return 0;
}
