/*
 * Exact Chinese Chess
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unofficial Kindle-focused Xiangqi adaptation. Code lineage, artwork
 * provenance, and Pikafish source notes are documented in docs/PROVENANCE.md.
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
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

static const char *kindle_window_title(void)
{
    const char *value = g_getenv("KINDLE_SHOW_TOPBAR");
    return (value != NULL && value[0] != '\0' && strcmp(value, "0") != 0) ? KINDLE_WINDOW_TITLE_TOPBAR
                                                                          : KINDLE_WINDOW_TITLE;
}

typedef struct {
    GtkWidget *window;
    GtkWidget *canvas;
    GtkWidget *board;
    GtkWidget *status;
    GtkWidget *status_panel;
    GtkWidget *mode_choices[3];
    GtkWidget *level_choices[3];
    GtkWidget *menu_panel;
    GtkWidget *menu_actions;
    GtkWidget *new_game_panel;
    GtkWidget *difficulty_panel;
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
static void position_overlays(void);
static void canvas_size_allocate(GtkWidget *widget, GtkAllocation *size);

typedef struct { GtkFixed parent; } ChessCanvas;
typedef struct { GtkFixedClass parent_class; } ChessCanvasClass;
G_DEFINE_TYPE(ChessCanvas, chess_canvas, GTK_TYPE_FIXED)

static void chess_canvas_class_init(ChessCanvasClass *klass) {
    GTK_WIDGET_CLASS(klass)->size_allocate = canvas_size_allocate;
}

static void chess_canvas_init(ChessCanvas *canvas) {
    (void)canvas;
}

static void menu_back_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    gtk_widget_hide(app.new_game_panel);
    gtk_widget_show(app.menu_actions);
    position_overlays();
    update_ui();
}

static void show_menu(void) {
    menu_back_cb(NULL, NULL);
    gtk_widget_show(app.menu_panel);
    position_overlays();
}

static void hide_menu(void) {
    gtk_widget_hide(app.menu_panel);
    update_ui();
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
    update_ui();
    return FALSE;
}

static void set_message(const char *message) {
    if (app.message_source != 0) {
        g_source_remove(app.message_source);
        app.message_source = 0;
    }
    g_strlcpy(app.message, message, sizeof(app.message));
    if (app.status == NULL) return;
    gtk_label_set_text(GTK_LABEL(app.status), message);
    if (message[0] == '\0') {
        update_ui();
    } else {
        gtk_widget_show(app.status);
        gtk_widget_show(app.status_panel);
        position_overlays();
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

static void app_install_kindle_style(void) {
    gtk_rc_parse_string(
        "gtk-font-name = \"Sans 24\"\n"
        "style \"kindle_high_contrast\" {\n"
        "  fg[NORMAL] = \"#000000\"\n"
        "  fg[ACTIVE] = \"#000000\"\n"
        "  fg[PRELIGHT] = \"#ffffff\"\n"
        "  fg[SELECTED] = \"#ffffff\"\n"
        "  text[NORMAL] = \"#000000\"\n"
        "  text[ACTIVE] = \"#000000\"\n"
        "  text[SELECTED] = \"#ffffff\"\n"
        "  base[NORMAL] = \"#ffffff\"\n"
        "  base[ACTIVE] = \"#ffffff\"\n"
        "  base[SELECTED] = \"#000000\"\n"
        "  bg[NORMAL] = \"#ffffff\"\n"
        "  bg[ACTIVE] = \"#ffffff\"\n"
        "  bg[PRELIGHT] = \"#000000\"\n"
        "  bg[SELECTED] = \"#000000\"\n"
        "}\n"
        "gtk-button-images = 0\n"
        "gtk-menu-images = 0\n"
        "style \"chess_flat\" {\n"
        "  fg[NORMAL] = \"#000000\"\n"
        "  fg[ACTIVE] = \"#000000\"\n"
        "  fg[PRELIGHT] = \"#000000\"\n"
        "  bg[NORMAL] = \"#ffffff\"\n"
        "  bg[ACTIVE] = \"#ffffff\"\n"
        "  bg[PRELIGHT] = \"#ffffff\"\n"
        "  GtkCheckButton::indicator-size = 24\n"
        "  GtkCheckButton::indicator-spacing = 12\n"
        "}\n"
        "widget \"*.chess_flat\" style \"chess_flat\"\n"
        "class \"GtkComboBox\" style \"kindle_high_contrast\"\n"
        "class \"GtkCellView\" style \"kindle_high_contrast\"\n"
        "class \"GtkMenu\" style \"kindle_high_contrast\"\n"
        "class \"GtkMenuItem\" style \"kindle_high_contrast\"\n"
        "widget_class \"*GtkComboBox*\" style \"kindle_high_contrast\"\n"
        "widget_class \"*GtkMenu*\" style \"kindle_high_contrast\"\n"
    );
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
    int i;
    (void)widget;
    (void)data;
    for (i = 0; i < 3; i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.mode_choices[i])))
            app.mode = (AppMode)i;
    for (i = 0; i < 3; i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.level_choices[i])))
            app.level = (XiangqiAiLevel)i;
    pikafish_uci_set_difficulty(&app.pikafish, app.level);
    new_game();
    hide_menu();
}

static void show_new_game_cb(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mode_choices[app.mode]), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.level_choices[app.level]), TRUE);
    gtk_widget_hide(app.menu_actions);
    gtk_widget_show(app.new_game_panel);
    position_overlays();
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
    (void)event;
    (void)data;

    cr = gdk_cairo_create(widget->window);
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

static gboolean board_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    gboolean open = gtk_widget_get_visible(app.menu_panel);
    TouchAction action;
    double x, y;
    (void)data;
    if (event->button != 1) return FALSE;
    touch_coordinates(widget, event->x, event->y, &x, &y);
    action = touch_input_end(&app.touch, x, y, open);
    if (action == TOUCH_OPEN_SYSTEM_MENU) show_system_menu();
    else if (action == TOUCH_OPEN_MENU) show_menu();
    else if (action == TOUCH_CLOSE_MENU || (action == TOUCH_TAP && open)) hide_menu();
    else if (action == TOUCH_TAP && widget == app.status_panel) show_menu();
    else if (action == TOUCH_TAP && widget == app.board) return board_tap(widget, event);
    return TRUE;
}

static void update_ui(void) {
    const char *status = app.message;
    if (app.status == NULL) return;
    if (!status[0]) {
        if (app.game.game_over)
            status = app.game.winner == XIANGQI_RED ? "红方获胜" : "黑方获胜";
        else
            status = app.game.turn == XIANGQI_RED ? "红方走棋" : "黑方走棋";
    }
    gtk_label_set_text(GTK_LABEL(app.status), status);
    gtk_widget_show(app.status);
    gtk_widget_show(app.status_panel);
    gtk_widget_queue_draw(app.board);
}

/* All overlays share the full-size board's parent; they never reserve board space. */
static void canvas_size_allocate(GtkWidget *widget, GtkAllocation *size) {
    GtkAllocation board = { size->x, size->y, size->width, size->height };
    GtkAllocation panel;
    GtkRequisition requested;
    /* Bypass GtkFixed's natural-size allocation: no intermediate corner frames. */
    widget->allocation = *size;
    if (!app.board || !app.menu_panel || !app.status_panel) return;
    gtk_widget_size_allocate(app.board, &board);
    panel.x = size->x + 24;
    panel.width = MAX(1, size->width - 48);
    /* Prepare hidden panels too, so their first mapped frame is already final. */
    gtk_widget_size_request(app.menu_panel, &requested);
    panel.height = requested.height;
    panel.y = size->y + MAX(24, size->height - panel.height - 24);
    gtk_widget_size_allocate(app.menu_panel, &panel);
    panel.y = size->y + 24;
    panel.height = 56;
    gtk_widget_size_allocate(app.status_panel, &panel);
}

static void position_overlays(void) {
    GtkAllocation size;
    if (!app.canvas || !app.board || !app.menu_panel || !app.status_panel) return;
    gtk_widget_get_allocation(app.canvas, &size);
    if (size.width <= 1 || size.height <= 1) return;
    canvas_size_allocate(app.canvas, &size);
}

static gboolean panel_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    GtkAllocation size;
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
    (void)event;
    (void)data;
    gtk_widget_get_allocation(widget, &size);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, size.width - 1, size.height - 1);
    cairo_stroke(cr);
    cairo_destroy(cr);
    return FALSE;
}

static gboolean button_divider_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    GtkAllocation size;
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
    (void)event;
    (void)data;
    gtk_widget_get_allocation(widget, &size);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, size.x + size.width - 0.5, size.y + 16);
    cairo_line_to(cr, size.x + size.width - 0.5, size.y + size.height - 16);
    cairo_stroke(cr);
    cairo_destroy(cr);
    return FALSE;
}

static gboolean menu_handle_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    GtkAllocation size;
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
    (void)event;
    (void)data;
    gtk_widget_get_allocation(widget, &size);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, size.width / 2.0 - 24, size.height / 2.0);
    cairo_line_to(cr, size.width / 2.0 + 24, size.height / 2.0);
    cairo_stroke(cr);
    cairo_destroy(cr);
    return FALSE;
}

static GtkWidget *add_button(GtkWidget *box, const char *label, GCallback callback) {
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_set_name(button, "chess_flat");
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(button, -1, 80);
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    g_signal_connect(button, "clicked", callback, NULL);
    return button;
}

static void choice_toggled_cb(GtkToggleButton *button, gpointer data) {
    (void)button;
    (void)data;
    if (app.difficulty_panel != NULL) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.mode_choices[MODE_TWO_PLAYER])))
            gtk_widget_hide(app.difficulty_panel);
        else
            gtk_widget_show(app.difficulty_panel);
        position_overlays();
    }
}

static GtkWidget *make_choice_row(const char **items, int count, GtkWidget **choices) {
    GtkWidget *row = gtk_hbox_new(TRUE, 12);
    GSList *group = NULL;
    int i;
    for (i = 0; i < count; i++) {
        choices[i] = gtk_radio_button_new_with_label(group, items[i]);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(choices[i]));
        gtk_widget_set_name(choices[i], "chess_flat");
        gtk_widget_set_size_request(choices[i], -1, 84);
        gtk_box_pack_start(GTK_BOX(row), choices[i], TRUE, TRUE, 0);
        g_signal_connect(choices[i], "toggled", G_CALLBACK(choice_toggled_cb), NULL);
        choice_toggled_cb(GTK_TOGGLE_BUTTON(choices[i]), NULL);
    }
    return row;
}

static void set_form_label_size(GtkWidget *widget, gpointer pixels) {
    if (GTK_IS_LABEL(widget)) {
        PangoAttrList *attributes = pango_attr_list_new();
        pango_attr_list_insert(attributes, pango_attr_size_new_absolute(
            GPOINTER_TO_INT(pixels) * PANGO_SCALE));
        gtk_label_set_attributes(GTK_LABEL(widget), attributes);
        pango_attr_list_unref(attributes);
    }
    if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach(GTK_CONTAINER(widget), set_form_label_size, pixels);
}

static void menu_undo_cb(GtkWidget *widget, gpointer data) {
    undo_cb(widget, data);
    hide_menu();
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

int main(int argc, char **argv) {
    GtkWidget *body, *row, *handle, *button;
    const char *modes[] = { "我执红", "我执黑", "双人对弈" };
    const char *levels[] = { "简单", "中等", "困难" };

    gtk_init(&argc, &argv);
    if (!prepare_data_paths()) return 1;
    app_install_kindle_style();
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
    app.canvas = g_object_new(chess_canvas_get_type(), NULL);
    gtk_container_add(GTK_CONTAINER(app.window), app.canvas);
    app.board = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.board, 240, 240);
    gtk_fixed_put(GTK_FIXED(app.canvas), app.board, 0, 0);
    g_signal_connect(app.board, "expose-event", G_CALLBACK(board_expose), NULL);
    connect_touch(app.board);

    app.menu_panel = gtk_event_box_new();
    app_apply_high_contrast(app.menu_panel);
    connect_touch(app.menu_panel);
    gtk_fixed_put(GTK_FIXED(app.canvas), app.menu_panel, 0, 0);
    g_signal_connect_after(app.menu_panel, "expose-event", G_CALLBACK(panel_expose), NULL);
    body = gtk_vbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(body), 16);
    gtk_container_add(GTK_CONTAINER(app.menu_panel), body);
    handle = gtk_drawing_area_new();
    app_apply_high_contrast(handle);
    gtk_widget_set_size_request(handle, -1, 28);
    g_signal_connect(handle, "expose-event", G_CALLBACK(menu_handle_expose), NULL);
    gtk_box_pack_start(GTK_BOX(body), handle, FALSE, FALSE, 0);
    connect_touch(handle);

    app.menu_actions = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(body), app.menu_actions, FALSE, FALSE, 0);
    button = add_button(app.menu_actions, "新局", G_CALLBACK(show_new_game_cb));
    g_signal_connect_after(button, "expose-event", G_CALLBACK(button_divider_expose), NULL);
    button = add_button(app.menu_actions, "悔棋", G_CALLBACK(menu_undo_cb));
    g_signal_connect_after(button, "expose-event", G_CALLBACK(button_divider_expose), NULL);
    add_button(app.menu_actions, "退出", G_CALLBACK(quit_cb));

    app.new_game_panel = gtk_vbox_new(FALSE, 12);
    gtk_box_pack_start(GTK_BOX(body), app.new_game_panel, FALSE, FALSE, 0);
    row = gtk_label_new("对弈方式");
    gtk_misc_set_alignment(GTK_MISC(row), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(app.new_game_panel), row, FALSE, FALSE, 0);
    row = make_choice_row(modes, 3, app.mode_choices);
    gtk_box_pack_start(GTK_BOX(app.new_game_panel), row, FALSE, FALSE, 0);
    app.difficulty_panel = gtk_vbox_new(FALSE, 12);
    gtk_box_pack_start(GTK_BOX(app.new_game_panel), app.difficulty_panel, FALSE, FALSE, 0);
    row = gtk_label_new("电脑难度");
    gtk_misc_set_alignment(GTK_MISC(row), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(app.difficulty_panel), row, FALSE, FALSE, 0);
    row = make_choice_row(levels, 3, app.level_choices);
    gtk_box_pack_start(GTK_BOX(app.difficulty_panel), row, FALSE, FALSE, 0);
    row = gtk_hbox_new(TRUE, 12);
    gtk_box_pack_start(GTK_BOX(app.new_game_panel), row, FALSE, FALSE, 0);
    add_button(row, "取消", G_CALLBACK(menu_back_cb));
    add_button(row, "开始新局", G_CALLBACK(new_game_cb));
    app.status_panel = gtk_event_box_new();
    app_apply_high_contrast(app.status_panel);
    app.status = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(app.status), PANGO_ELLIPSIZE_END);
    gtk_container_add(GTK_CONTAINER(app.status_panel), app.status);
    gtk_fixed_put(GTK_FIXED(app.canvas), app.status_panel, 0, 0);
    connect_touch(app.status_panel);
    set_form_label_size(app.canvas, GINT_TO_POINTER(32));

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
    gtk_widget_show_all(app.menu_panel);
    gtk_widget_hide(app.new_game_panel);
    gtk_widget_hide(app.menu_panel);
    gtk_widget_set_no_show_all(app.menu_panel, TRUE);
    gtk_widget_show_all(app.window);
    gtk_window_present(GTK_WINDOW(app.window));
    position_overlays();
    maybe_schedule_ai();
    gtk_main();
    if (app.message_source != 0) g_source_remove(app.message_source);
    pikafish_uci_clear(&app.pikafish);
    clear_assets();
    g_free(app.save_path);
    g_free(app.log_path);
    return 0;
}
