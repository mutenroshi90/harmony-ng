#include "remote_ui.h"
#include "remote_fb.h"
#include "remote_config.h"
#include "remote_rf.h"
#include "remote_ui_scroll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A row's label from text_x, cut to fit before the chevron; in the title font and centred on
   the row when the theme asks for large labels */
static void draw_row_label(int text_x, int y, int row_h, const char *label, uint16_t color) {
    const font_aa_t *f = g_theme.large_labels ? &g_font_title : &g_font_main;
    int ty = g_theme.large_labels ? y + (row_h - f->ascent) / 2 : y + 9;
    char fit[RF_NAME_SIZE];
    fb_fit_text_aa(fit, sizeof(fit), label, f, FB_WIDTH - 8 - text_x - (g_theme.chevrons ? 22 : 8));
    fb_draw_text_aa(text_x, ty, fit, color, f);
}

void execute_active_dev_cmd(int filtered_idx) {
    char label[32] = {0};
    uint8_t btn_idx = 0, atype = 1;
    if (get_active_dev_cmd_item(filtered_idx, label, sizeof(label), &btn_idx, &atype, NULL, 0) == 0) {
        if (atype == 2) {
            trigger_activity_switch(g_switching_target_id, label);
        } else if (atype == 3) {
            trigger_activity_switch("-1", "PowerOff");
        } else {
            const char *ctx = g_active_activity_id;
            rf_send_screen_button_press(ctx, btn_idx, label);
        }
    }
}

/* Whether the running activity shows its touch buttons in two columns (set per activity in the
   Hub's web UI) */
bool devices_screen_grid(void) {
    const rf_activity_t *act = rf_activity_by_id(g_active_activity_id);
    return act && act->screen_grid;
}

/* The rows of the two-column view: a wide button fills a row, two narrow ones in a row share
   one, and a narrow one before a wide one (or the last one) keeps its half alone. Fills the row
   and column (0 left, 1 right) of each button and returns the number of rows. */
static short s_grid_row[MAX_RF_BUTTONS];
static unsigned char s_grid_col[MAX_RF_BUTTONS], s_grid_wide[MAX_RF_BUTTONS];

static int grid_layout(int total) {
    if (total > MAX_RF_BUTTONS) total = MAX_RF_BUTTONS;
    int row = 0, col = 0;
    for (int i = 0; i < total; i++) {
        bool wide = get_active_dev_cmd_wide(i);
        if (wide && col) { row++; col = 0; } /* a wide button starts its own row */
        s_grid_row[i] = (short)row;
        s_grid_col[i] = (unsigned char)col;
        s_grid_wide[i] = wide;
        if (wide || col) { row++; col = 0; }
        else col = 1;
    }
    return total ? row + (col ? 1 : 0) : 0;
}

int devices_grid_rows(int *sel_row) {
    int total = get_active_dev_cmd_count();
    int rows = grid_layout(total);
    if (sel_row) *sel_row = (g_selected_device_cmd >= 0 && g_selected_device_cmd < total && g_selected_device_cmd < MAX_RF_BUTTONS)
                                ? s_grid_row[g_selected_device_cmd] : 0;
    return rows;
}

/* The button under a tap in the two-column view: the row from the list, the column from x */
int devices_grid_index_at(int x, int y) {
    int row = list_row_at(y);
    if (row < 0) return -1;
    int total = grid_layout(get_active_dev_cmd_count());
    int col = x >= FB_WIDTH / 2 ? 1 : 0;
    for (int i = 0; i < total && i < MAX_RF_BUTTONS; i++) {
        if (s_grid_row[i] == row && (s_grid_wide[i] || s_grid_col[i] == col)) return i;
    }
    return -1;
}

void render_devices_screen(void) {
    int start_y = UI_CONTENT_Y;
    int card_h = LIST_ROW_H;
    int total = get_active_dev_cmd_count();

    if (total <= 0) {
        if (render_activity_hero()) return;
        fb_draw_text_aa(8, start_y + 20, tr(STR_NO_COMMANDS), RGB_WHITE, &g_font_title);
        fb_draw_text_aa(8, start_y + 45, tr(STR_CONFIGURE_IN_WEBUI), RGB_CYAN, &g_font_main);
        fb_draw_text_aa(8, start_y + 65, tr(STR_FOR_THIS_ACTIVITY), RGB_TEXT_MUTED, &g_font_main);
        return;
    }

    list_geom_t g;
    list_geometry(&g);
    int scroll = list_scroll_offset();
    int first = scroll > 0 ? scroll / LIST_ROW_PITCH : 0;
    fb_clip_rows(g.y, g.y + g.rows * LIST_ROW_PITCH);

    if (devices_screen_grid()) {
        /* Two columns: a row holds two narrow buttons or one wide one, the label centred */
        int half_w = (FB_WIDTH - 16 - DEV_GRID_GAP) / 2;
        int n = grid_layout(total) ? (total < MAX_RF_BUTTONS ? total : MAX_RF_BUTTONS) : 0;
        for (int idx = 0; idx < n; idx++) {
            if (s_grid_row[idx] < first) continue;
            int y = start_y + s_grid_row[idx] * LIST_ROW_PITCH - scroll;
            if (y >= g.y + g.rows * LIST_ROW_PITCH) break;
            int col_w = s_grid_wide[idx] ? FB_WIDTH - 16 : half_w;
            int x = 8 + s_grid_col[idx] * (half_w + DEV_GRID_GAP);
            bool is_sel = (idx == g_selected_device_cmd);

            char label[32] = {0};
            uint8_t bidx = 0, atype = 1;
            get_active_dev_cmd_item(idx, label, sizeof(label), &bidx, &atype, NULL, 0);

            fb_fill_round_rect(x, y, col_w, card_h, g_theme.corner_radius, is_sel ? RGB_CARD_SEL : RGB_CARD);
            fb_draw_round_rect(x, y, col_w, card_h, g_theme.corner_radius, is_sel ? RGB_TEAL : g_theme.pal.outline);

            const font_aa_t *f = g_theme.large_labels ? &g_font_title : &g_font_main;
            if (f == &g_font_title && fb_text_width_aa(label, f) > col_w - 10) {
                f = &g_font_main; /* a long label keeps more of its text in the smaller font */
            }
            char fit[RF_NAME_SIZE];
            fb_fit_text_aa(fit, sizeof(fit), label, f, col_w - 10);
            int ty = g_theme.large_labels ? y + (card_h - f->ascent) / 2 : y + 9;
            fb_draw_text_aa(x + (col_w - fb_text_width_aa(fit, f)) / 2, ty, fit, is_sel ? RGB_WHITE : (g_theme.uniform_lists ? RGB_TEXT_MUTED : RGB_CYAN), f);
        }
        fb_clip_reset();
        render_list_scrollbar();
        return;
    }

    for (int idx = first; idx < total; idx++) {
        int y = start_y + idx * LIST_ROW_PITCH - scroll;
        if (y >= g.y + g.rows * LIST_ROW_PITCH) break;
        bool is_sel = (idx == g_selected_device_cmd);
        uint16_t bg = is_sel ? RGB_CARD_SEL : RGB_CARD;
        uint16_t border = is_sel ? RGB_TEAL : g_theme.pal.outline;

        char label[32] = {0};
        char icon_key[24] = {0};
        uint8_t bidx = 0, atype = 1;
        get_active_dev_cmd_item(idx, label, sizeof(label), &bidx, &atype, icon_key, sizeof(icon_key));

        fb_fill_round_rect(8, y, FB_WIDTH - 16, card_h, g_theme.corner_radius, bg);
        fb_draw_round_rect(8, y, FB_WIDTH - 16, card_h, g_theme.corner_radius, border);

        char btn_icon_path[128] = {0};
        bool has_btn_icon = resolve_button_icon(icon_key, label, btn_icon_path, sizeof(btn_icon_path));
        if (has_btn_icon) {
            has_btn_icon = blit_png_file(14, y + (card_h - 22) / 2, 22, 22, btn_icon_path);
        }

        int text_x = has_btn_icon ? 42 : 16;
        draw_row_label(text_x, y, card_h, label, is_sel ? RGB_WHITE : (g_theme.uniform_lists ? RGB_TEXT_MUTED : RGB_CYAN));

        if (is_sel && g_theme.chevrons) {
            fb_draw_text_aa(FB_WIDTH - 24, y + 8, ">", RGB_CYAN, &g_font_title);
        }
    }
    fb_clip_reset();
    render_list_scrollbar();
}

/* Inside a device the rows are its commands, below a heading with the device's name */
int browser_item_count(void) {
    if (g_browse_dev < 0) return g_rf_device_count;
    int n = 0;
    for (int i = 0; i < g_rf_dev_cmd_count; i++) {
        if (g_rf_dev_cmds[i].dev_idx == g_browse_dev) n++;
    }
    return n;
}

/* The command on row n of the browsed device, in the order the Hub sent them */
const rf_dev_cmd_t *browser_cmd(int n) {
    for (int i = 0; i < g_rf_dev_cmd_count; i++) {
        if (g_rf_dev_cmds[i].dev_idx == g_browse_dev && n-- == 0) return &g_rf_dev_cmds[i];
    }
    return NULL;
}

int browser_page_size(void) {
    return g_browse_dev >= 0 ? LIST_PAGE_SIZE - 1 : LIST_PAGE_SIZE;
}

int browser_list_y(void) {
    return g_browse_dev >= 0 ? SETTINGS_LIST_Y : UI_CONTENT_Y;
}

/* The screen showing the activity card a device was opened from, or -1 */
static int s_card_view = -1;

void open_device_browser(void) {
    g_current_view = VIEW_DEVICE_BROWSER;
    g_browse_dev = -1;
    g_browse_sel = 0;
    s_card_view = -1;
}

void open_device_from_card(int dev) {
    if (dev < 0 || dev >= g_rf_device_count) return;
    s_card_view = (int)g_current_view;
    g_current_view = VIEW_DEVICE_BROWSER;
    g_browse_dev = dev;
    g_browse_sel = 0;
}

/* From a device back to the card it was opened from, or to the device list; from the list to
   the running activity, or home */
void device_browser_back(void) {
    if (g_browse_dev >= 0 && s_card_view >= 0) {
        g_current_view = (ui_view_t)s_card_view;
        g_browse_dev = -1;
        s_card_view = -1;
    } else if (g_browse_dev >= 0) {
        g_browse_sel = g_browse_dev;
        g_browse_dev = -1;
    } else if (g_active_activity_id[0] && strcmp(g_active_activity_id, "-1") != 0) {
        g_current_view = VIEW_DEVICES;
    } else {
        g_current_view = VIEW_ACTIVITIES;
    }
}

void device_browser_move(int delta) {
    int n = browser_item_count();
    if (n > 0) g_browse_sel = ((g_browse_sel + delta) % n + n) % n;
}

/* Open a device from the list, or send the chosen command to the Hub */
void device_browser_activate(int n) {
    if (n < 0 || n >= browser_item_count()) return;
    g_browse_sel = n;
    if (g_browse_dev < 0) {
        g_browse_dev = n;
        g_browse_sel = 0;
        return;
    }
    const rf_dev_cmd_t *c = browser_cmd(n);
    if (!c || g_rf_fd < 0) return;
    uint8_t payload[25] = {0};
    memcpy(payload, g_rf_devices[g_browse_dev].id, strnlen(g_rf_devices[g_browse_dev].id, 4));
    payload[4] = c->cmd_idx;
    memcpy(&payload[5], c->name, strnlen(c->name, 20));
    rf_frame_t f;
    rf_build_frame(&f, g_remote_slot, RF_MSG_DEVICE_CMD_INDEX, ++g_tx_seq, 0, payload, sizeof(payload));
    rf_send_frame(&f);
    printf("[*] RF TX DEVICE_CMD_INDEX: dev=%s idx=%d cmd=%s\n", g_rf_devices[g_browse_dev].id, c->cmd_idx, c->name);
}

/* Render the device list, or the browsed device's heading and commands */
void render_device_browser_view(void) {
    if (g_browse_dev >= g_rf_device_count) g_browse_dev = -1;
    int start_y = browser_list_y();
    int card_h = LIST_ROW_H;
    int total = browser_item_count();
    if (g_browse_sel >= total) g_browse_sel = 0;

    if (g_browse_dev >= 0) {
        char fit[RF_NAME_SIZE];
        fb_draw_text_aa(14, SETTINGS_HEADING_Y, fb_fit_text_aa(fit, sizeof(fit), g_rf_devices[g_browse_dev].name, &g_font_title, FB_WIDTH - 28), RGB_CYAN, &g_font_title);
        if (total <= 0) {
            fb_draw_text_aa(14, start_y + 10, tr(STR_CONFIGURE_IN_WEBUI), RGB_TEXT_MUTED, &g_font_main);
            return;
        }
    }

    if (total <= 0) {
        fb_draw_text_aa(8, start_y + 20, tr(STR_NO_DEVICES), RGB_WHITE, &g_font_title);
        fb_draw_text_aa(8, start_y + 45, tr(STR_CONFIGURE_IN_WEBUI), RGB_CYAN, &g_font_main);
        return;
    }

    list_geom_t g;
    list_geometry(&g);
    int scroll = list_scroll_offset();
    int first = scroll > 0 ? scroll / LIST_ROW_PITCH : 0;
    fb_clip_rows(g.y, g.y + g.rows * LIST_ROW_PITCH);
    for (int idx = first; idx < total; idx++) {
        int y = start_y + idx * LIST_ROW_PITCH - scroll;
        if (y >= g.y + g.rows * LIST_ROW_PITCH) break;
        bool is_sel = (idx == g_browse_sel);
        const rf_dev_cmd_t *c = (g_browse_dev >= 0) ? browser_cmd(idx) : NULL;
        const char *label = (g_browse_dev < 0) ? g_rf_devices[idx].name : (c ? c->name : "");

        fb_fill_round_rect(8, y, FB_WIDTH - 16, card_h, g_theme.corner_radius, is_sel ? RGB_CARD_SEL : RGB_CARD);
        fb_draw_round_rect(8, y, FB_WIDTH - 16, card_h, g_theme.corner_radius, is_sel ? RGB_TEAL : g_theme.pal.outline);

        /* Device images are 45 px line art, drawn at 28 px; command glyphs stay at 22 px */
        int icon_size = g_browse_dev < 0 ? 28 : 22;
        char dev_icon_path[128] = {0};
        bool has_icon = false;
        if (g_browse_dev < 0) {
            if (resolve_device_icon(g_rf_devices[idx].icon_id, g_rf_devices[idx].icon, g_rf_devices[idx].name, g_rf_devices[idx].id, dev_icon_path, sizeof(dev_icon_path))) {
                has_icon = blit_png_file(14, y + (card_h - icon_size) / 2, icon_size, icon_size, dev_icon_path);
            }
        } else {
            if (resolve_button_icon(NULL, label, dev_icon_path, sizeof(dev_icon_path))) {
                has_icon = blit_png_file(14, y + (card_h - icon_size) / 2, icon_size, icon_size, dev_icon_path);
            }
        }

        int text_x = has_icon ? 14 + icon_size + 6 : 16;
        draw_row_label(text_x, y, card_h, label, is_sel ? RGB_WHITE : (g_theme.uniform_lists ? RGB_TEXT_MUTED : RGB_CYAN));
        if (g_browse_dev < 0 && g_theme.chevrons) {
            fb_draw_text_aa(FB_WIDTH - 24, y + 8, ">", is_sel ? RGB_CYAN : RGB_TEXT_MUTED, &g_font_title);
        }
    }
    fb_clip_reset();
    render_list_scrollbar();
}

/* A thumb at the right edge of the list, sized to the share of rows on screen */
void render_list_scrollbar(void) {
    list_geom_t g;
    if (!list_geometry(&g) || g.count <= g.rows) return;
    int track = g.rows * LIST_ROW_PITCH - LIST_ROW_GAP;
    int max = (g.count - g.rows) * LIST_ROW_PITCH;
    int thumb = track * g.rows / g.count;
    if (thumb < 16) thumb = 16;
    int scroll = list_scroll_offset();
    if (scroll < 0) scroll = 0;
    if (scroll > max) scroll = max;
    fb_fill_rect(FB_WIDTH - 5, g.y + (track - thumb) * scroll / max, 3, thumb, RGB_TEXT_MUTED);
}
