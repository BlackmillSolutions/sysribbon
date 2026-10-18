/* SPDX-License-Identifier: MIT */
/* SysRibbon: lightweight Linux /proc + /sys monitor for the Xfce panel. */
#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4util/libxfce4util.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sampler.h"
#include "presentation.h"

#define DEFAULT_INTERVAL_MS 2000U
#define MIN_INTERVAL_MS 100U
#define MAX_INTERVAL_MS 15000U
#define DEFAULT_POINTS 8U
#define MIN_SECTION_WIDTH 42
#define MAX_SECTION_WIDTH 520
#define EDGE_GRAB_PX 8

typedef struct _Applet Applet;

typedef struct {
    Applet *owner;
    unsigned index;
    GtkWidget *event_box; /* Own input-only event window for local cursor changes. */
    GtkWidget *canvas;    /* Fixed-size Cairo/Pango rendering; no label size jitter. */
    PangoLayout *layout;
    gboolean dragging;
    gboolean edge_hover;
    double drag_origin;
    int width_at_press;
} Section;

struct _Applet {
    XfcePanelPlugin *plugin;
    GtkWidget *row;
    GtkWidget *empty_placeholder;
    Section section[SR_SECTION_COUNT];
    guint timeout_id;
    unsigned interval_ms;
    gboolean visible[SR_SECTION_COUNT];
    int width[SR_SECTION_COUNT];
    int minimal_width[SR_SECTION_COUNT];
    gboolean minimal;
    gboolean vertical;
    SrDisplayOptions display;
    SrSampler sampler;
    SrSample current;
    SrHistory history[3];
    GdkCursor *resize_h;
    GdkCursor *resize_v;
};

static const char *const width_names[] = {"width-cpu", "width-ram", "width-temp", "width-network"};
static const char *const minimal_width_names[] = {"minimal-width-cpu", "minimal-width-ram", "minimal-width-temp", "minimal-width-network"};
static const int width_defaults[] = {162, 222, 172, 226};
static const int minimal_width_defaults[] = {68, 138, 82, 194};
static const char *const show_names[] = {"show-cpu", "show-ram", "show-temp", "show-network"};
static const char *const warn_names[] = {"cpu-warning", "ram-warning", "temp-warning"};
static const char *const crit_names[] = {"cpu-critical", "ram-critical", "temp-critical"};
static const char *const floor_names[] = {"cpu-floor-pct", "ram-floor-pct", "temp-floor-c"};
static const double floor_defaults[] = {0.0, 0.0, 30.0};

static int *active_width(Applet *a, unsigned index) {
    return a->minimal ? &a->minimal_width[index] : &a->width[index];
}

static void save_settings(XfcePanelPlugin *plugin, Applet *a) {
    gchar *path = xfce_panel_plugin_save_location(plugin, TRUE);
    if (!path) return;
    XfceRc *rc = xfce_rc_simple_open(path, FALSE);
    g_free(path);
    if (!rc) return;
    xfce_rc_write_int_entry(rc, "interval-ms", (gint)a->interval_ms);
    xfce_rc_write_int_entry(rc, "history-points", (gint)a->display.points);
    xfce_rc_write_bool_entry(rc, "show-colors", a->display.colors);
    xfce_rc_write_bool_entry(rc, "show-graphs", a->display.graphs);
    xfce_rc_write_bool_entry(rc, "minimal-mode", a->minimal);
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        xfce_rc_write_bool_entry(rc, show_names[i], a->visible[i]);
        xfce_rc_write_int_entry(rc, width_names[i], a->width[i]);
        xfce_rc_write_int_entry(rc, minimal_width_names[i], a->minimal_width[i]);
    }
    for (unsigned i = 0; i < 3; ++i) {
        xfce_rc_write_int_entry(rc, warn_names[i], (gint)a->display.warning[i]);
        xfce_rc_write_int_entry(rc, crit_names[i], (gint)a->display.critical[i]);
        xfce_rc_write_int_entry(rc, floor_names[i], (gint)a->display.floor[i]);
    }
    xfce_rc_close(rc);
}

static void read_settings(Applet *a) {
    a->interval_ms = DEFAULT_INTERVAL_MS;
    a->display.points = DEFAULT_POINTS;
    a->display.colors = TRUE;
    a->display.graphs = TRUE;
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        a->visible[i] = TRUE;
        a->width[i] = width_defaults[i];
        a->minimal_width[i] = minimal_width_defaults[i];
    }
    a->display.warning[0] = 50; a->display.critical[0] = 80;
    a->display.warning[1] = 70; a->display.critical[1] = 90;
    a->display.warning[2] = 65; a->display.critical[2] = 85;
    for (unsigned i = 0; i < 3; ++i) a->display.floor[i] = floor_defaults[i];
    gchar *path = xfce_panel_plugin_save_location(a->plugin, FALSE);
    if (!path) return;
    XfceRc *rc = xfce_rc_simple_open(path, TRUE);
    g_free(path);
    if (!rc) return;
    /* Migrate v0.1/v0.2 seconds setting without losing users' choices. */
    int legacy_seconds = CLAMP(xfce_rc_read_int_entry(rc, "interval", 2), 1, 15);
    a->interval_ms = (unsigned)CLAMP(xfce_rc_read_int_entry(rc, "interval-ms", legacy_seconds * 1000),
                                    (int)MIN_INTERVAL_MS, (int)MAX_INTERVAL_MS);
    a->interval_ms = ((a->interval_ms + 50U) / 100U) * 100U;
    a->display.points = CLAMP(xfce_rc_read_int_entry(rc, "history-points", DEFAULT_POINTS), 4, SR_HISTORY_CAP);
    a->display.colors = xfce_rc_read_bool_entry(rc, "show-colors", TRUE);
    a->display.graphs = xfce_rc_read_bool_entry(rc, "show-graphs", TRUE);
    a->minimal = xfce_rc_read_bool_entry(rc, "minimal-mode", FALSE);
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        a->visible[i] = xfce_rc_read_bool_entry(rc, show_names[i], TRUE);
        a->width[i] = CLAMP(xfce_rc_read_int_entry(rc, width_names[i], width_defaults[i]),
                            MIN_SECTION_WIDTH, MAX_SECTION_WIDTH);
        a->minimal_width[i] = CLAMP(xfce_rc_read_int_entry(rc, minimal_width_names[i], minimal_width_defaults[i]),
                                    MIN_SECTION_WIDTH, MAX_SECTION_WIDTH);
    }
    for (unsigned i = 0; i < 3; ++i) {
        int max = i == SR_SECTION_TEMP ? 125 : 100;
        a->display.warning[i] = CLAMP(xfce_rc_read_int_entry(rc, warn_names[i],
                                           (gint)a->display.warning[i]), 0, max);
        a->display.critical[i] = CLAMP(xfce_rc_read_int_entry(rc, crit_names[i],
                                            (gint)a->display.critical[i]),
                                            (int)a->display.warning[i], max);
        /* New in v0.4: old settings remain unchanged; missing floors use defaults. */
        a->display.floor[i] = CLAMP(xfce_rc_read_int_entry(rc, floor_names[i],
                                           (gint)floor_defaults[i]),
                                           i == SR_SECTION_TEMP ? -20 : 0, 95);
    }
    xfce_rc_close(rc);
}

static void apply_section_size(Section *s) {
    Applet *a = s->owner;
    int size = *active_width(a, s->index);
    /* DrawingArea has no text-dependent preferred width. */
    gtk_widget_set_size_request(s->canvas, a->vertical ? 18 : size,
                                a->vertical ? size : 18);
    gtk_widget_set_size_request(s->event_box, a->vertical ? -1 : size,
                                a->vertical ? size : -1);
    gtk_widget_queue_draw(s->canvas);
}

static void update_layout(Applet *a) {
    gboolean any_visible = FALSE;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(a->row),
                                   a->vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        Section *s = &a->section[i];
        if (!s->event_box) continue;
        apply_section_size(s);
        if (a->visible[i]) { gtk_widget_show(s->event_box); any_visible = TRUE; }
        else gtk_widget_hide(s->event_box);
    }
    if (a->empty_placeholder) {
        if (any_visible) gtk_widget_hide(a->empty_placeholder);
        else gtk_widget_show(a->empty_placeholder);
    }
}

static gboolean section_draw(GtkWidget *widget, cairo_t *cr, Section *s) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int w = allocation.width, h = allocation.height;
    if (w <= 0 || h <= 0) return FALSE;
    gboolean vertical = s->owner->vertical;
    int length = vertical ? h : w;
    int thick = vertical ? w : h;
    int available = MAX(1, length - EDGE_GRAB_PX - 7);
    pango_layout_set_width(s->layout, available * PANGO_SCALE);
    pango_layout_set_ellipsize(s->layout, PANGO_ELLIPSIZE_END);
    int text_w, text_h;
    pango_layout_get_pixel_size(s->layout, &text_w, &text_h);
    (void)text_w;
    cairo_save(cr);
    if (vertical) {
        cairo_translate(cr, 0, h);
        cairo_rotate(cr, -1.5707963267948966);
    }
    gtk_render_layout(gtk_widget_get_style_context(widget), cr, 4,
                      MAX(0, (thick - text_h) / 2), s->layout);
    cairo_restore(cr);

    /* Subtle separator on the actual section boundary; no visible grip symbol. */
    GtkStyleContext *style = gtk_widget_get_style_context(widget);
    GdkRGBA color;
    gtk_style_context_get_color(style, gtk_style_context_get_state(style), &color);
    double alpha = s->edge_hover || s->dragging ? 0.58 : 0.20;
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, alpha);
    cairo_set_line_width(cr, 1.0);
    if (vertical) {
        cairo_move_to(cr, 2, h - 0.5);
        cairo_line_to(cr, MAX(2, w - 2), h - 0.5);
    } else {
        cairo_move_to(cr, w - 0.5, 2);
        cairo_line_to(cr, w - 0.5, MAX(2, h - 2));
    }
    cairo_stroke(cr);
    return FALSE;
}

static void applet_render(Applet *a) {
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        if (!a->visible[i]) continue;
        char markup[1024];
        SrDisplayOptions opts = a->display;
        if (a->minimal) { opts.graphs = false; opts.minimal = true; }
        sr_format_section(markup, sizeof(markup), i, &a->current,
                          i < 3 ? &a->history[i] : NULL, &opts, false);
        pango_layout_set_markup(a->section[i].layout, markup, -1);
        gtk_widget_queue_draw(a->section[i].canvas);
    }
}

static gboolean applet_tick(gpointer data) {
    Applet *a = data;
    unsigned flags = (a->visible[0] ? SR_CPU : 0) | (a->visible[1] ? SR_RAM : 0) |
                     (a->visible[2] ? SR_TEMP : 0) | (a->visible[3] ? SR_NET : 0);
    sr_sample_selected(&a->sampler, &a->current, flags);
    if (a->display.graphs && !a->minimal) {
        if (a->visible[0]) sr_history_add(&a->history[0], a->current.cpu_ok, a->current.cpu_pct);
        if (a->visible[1]) sr_history_add(&a->history[1], a->current.ram_ok, a->current.ram_pct);
        if (a->visible[2]) sr_history_add(&a->history[2], a->current.temp_ok, a->current.temp_c);
    }
    applet_render(a);
    return G_SOURCE_CONTINUE;
}

static void reset_timer(Applet *a) {
    if (a->timeout_id) g_source_remove(a->timeout_id);
    /* Real millisecond timer: 100, 200, ... 15000 ms; no separate polling thread. */
    a->timeout_id = g_timeout_add(a->interval_ms, applet_tick, a);
}

static void set_section_width(Section *s, int width) {
    width = CLAMP(width, MIN_SECTION_WIDTH, MAX_SECTION_WIDTH);
    int *slot = active_width(s->owner, s->index);
    if (*slot == width) return;
    *slot = width;
    apply_section_size(s);
}

static gboolean in_resize_edge(Section *s, double x, double y) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(s->event_box, &allocation);
    double coord = s->owner->vertical ? y : x;
    int len = s->owner->vertical ? allocation.height : allocation.width;
    return coord >= (double)(len - EDGE_GRAB_PX) && coord < len && coord >= 0;
}

static void set_edge_cursor(Section *s, gboolean on, GdkWindow *event_window) {
    on = on || s->dragging;
    /* With above-child event boxes the pointer may be in a separate input-only
     * child GdkWindow. Use the actual event window, never the panel window. */
    if (event_window) gdk_window_set_cursor(event_window, on ?
        (s->owner->vertical ? s->owner->resize_v : s->owner->resize_h) : NULL);
    if (on != s->edge_hover) {
        s->edge_hover = on;
        gtk_widget_queue_draw(s->canvas);
    }
}

static gboolean section_motion(GtkWidget *widget, GdkEventMotion *event, Section *s) {
    (void)widget;
    if (s->dragging) {
        double coord = s->owner->vertical ? event->y_root : event->x_root;
        set_section_width(s, s->width_at_press + (int)round(coord - s->drag_origin));
    } else {
        set_edge_cursor(s, in_resize_edge(s, event->x, event->y), event->window);
    }
    return s->dragging;
}

static gboolean section_enter(GtkWidget *widget, GdkEventCrossing *event, Section *s) {
    (void)widget;
    if (!s->dragging) set_edge_cursor(s, in_resize_edge(s, event->x, event->y), event->window);
    return FALSE;
}

static gboolean section_leave(GtkWidget *widget, GdkEventCrossing *event, Section *s) {
    (void)widget; (void)event;
    if (!s->dragging) set_edge_cursor(s, FALSE, event->window);
    return FALSE;
}

static gboolean section_press(GtkWidget *widget, GdkEventButton *event, Section *s) {
    if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_BUTTON_PRESS ||
        !in_resize_edge(s, event->x, event->y)) return FALSE;
    s->dragging = TRUE;
    s->drag_origin = s->owner->vertical ? event->y_root : event->x_root;
    s->width_at_press = *active_width(s->owner, s->index);
    set_edge_cursor(s, TRUE, event->window);
    gtk_grab_add(widget);
    return TRUE;
}

static gboolean section_release(GtkWidget *widget, GdkEventButton *event, Section *s) {
    if (!s->dragging || event->button != GDK_BUTTON_PRIMARY) return FALSE;
    /* Motion events can be coalesced: account for final release coordinate. */
    double coord = s->owner->vertical ? event->y_root : event->x_root;
    set_section_width(s, s->width_at_press + (int)round(coord - s->drag_origin));
    s->dragging = FALSE;
    if (gtk_widget_has_grab(widget)) gtk_grab_remove(widget);
    set_edge_cursor(s, in_resize_edge(s, event->x, event->y), event->window);
    save_settings(s->owner->plugin, s->owner); /* Persist only at release. */
    return TRUE;
}

static gboolean section_grab_broken(GtkWidget *widget, GdkEventGrabBroken *event, Section *s) {
    (void)widget; (void)event;
    s->dragging = FALSE;
    set_edge_cursor(s, FALSE, event->window);
    return FALSE;
}

static GtkWidget *spin_at(GtkGrid *grid, int row, int col, double value,
                          double minimum, double maximum) {
    GtkWidget *w = gtk_spin_button_new_with_range(minimum, maximum, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), value);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(grid, w, col, row, 1, 1);
    return w;
}

static GtkWidget *settings_grid(void) {
    GtkWidget *widget = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(widget), 9);
    gtk_grid_set_column_spacing(GTK_GRID(widget), 12);
    gtk_container_set_border_width(GTK_CONTAINER(widget), 12);
    return widget;
}

static void minimal_toggled(GtkToggleButton *button, GtkWidget *graph_checkbox) {
    gtk_widget_set_sensitive(graph_checkbox, !gtk_toggle_button_get_active(button));
}

static void options_dialog(XfcePanelPlugin *plugin, Applet *a) {
    GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(plugin));
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "SysRibbon – Eigenschaften",
        GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Abbrechen", GTK_RESPONSE_CANCEL, "_Speichern", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, -1);

    GtkWidget *tabs = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), tabs);
    GtkWidget *general_widget = settings_grid();
    GtkGrid *general = GTK_GRID(general_widget);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), general_widget, gtk_label_new("Anzeige"));

    const char *const show_labels[] = {"CPU anzeigen", "RAM anzeigen", "Temperatur anzeigen", "Netzwerk anzeigen"};
    GtkWidget *shows[SR_SECTION_COUNT];
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        shows[i] = gtk_check_button_new_with_label(show_labels[i]);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shows[i]), a->visible[i]);
        gtk_grid_attach(general, shows[i], 0, (int)i, 2, 1);
    }
    GtkWidget *colors = gtk_check_button_new_with_label("Dynamische Farben verwenden");
    GtkWidget *graphs = gtk_check_button_new_with_label("Verlaufsgrafiken anzeigen");
    GtkWidget *minimal = gtk_check_button_new_with_label("Minimalmodus: nur Messwerte, ohne Bezeichnungen oder Verlauf");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(colors), a->display.colors);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphs), a->display.graphs);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(minimal), a->minimal);
    gtk_grid_attach(general, colors, 0, 4, 2, 1);
    gtk_grid_attach(general, graphs, 0, 5, 2, 1);
    gtk_grid_attach(general, minimal, 0, 6, 2, 1);
    g_signal_connect(minimal, "toggled", G_CALLBACK(minimal_toggled), graphs);
    minimal_toggled(GTK_TOGGLE_BUTTON(minimal), graphs);

    gtk_grid_attach(general, gtk_label_new("Intervall (Sekunden)"), 0, 7, 1, 1);
    GtkWidget *interval = gtk_spin_button_new_with_range(0.1, 15.0, 0.1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(interval), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(interval), a->interval_ms / 1000.0);
    gtk_grid_attach(general, interval, 1, 7, 1, 1);
    gtk_grid_attach(general, gtk_label_new("Verlauf (Messpunkte)"), 0, 8, 1, 1);
    GtkWidget *points = spin_at(general, 8, 1, a->display.points, 4, SR_HISTORY_CAP);
    GtkWidget *info = gtk_label_new("Größe ändern: direkt an der rechten Kante eines Bereichs ziehen.\n"
                                    "Kein sichtbarer Griff; der Resize-Cursor erscheint nur an der Kante.\n"
                                    "100 ms sind möglich, aber nicht als Echtzeit-Takt garantiert.");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0);
    gtk_grid_attach(general, info, 0, 9, 2, 1);

    GtkWidget *sizes_widget = settings_grid();
    GtkGrid *sizes = GTK_GRID(sizes_widget);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), sizes_widget, gtk_label_new("Breiten & Grenzwerte"));
    GtkWidget *widths[SR_SECTION_COUNT], *minimal_widths[SR_SECTION_COUNT];
    const char *const width_labels[] = {"CPU (px)", "RAM (px)", "Temperatur (px)", "Netzwerk (px)"};
    gtk_grid_attach(sizes, gtk_label_new("Bereich"), 0, 0, 1, 1);
    gtk_grid_attach(sizes, gtk_label_new("Normal"), 1, 0, 1, 1);
    gtk_grid_attach(sizes, gtk_label_new("Minimal"), 2, 0, 1, 1);
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        gtk_grid_attach(sizes, gtk_label_new(width_labels[i]), 0, (int)i + 1, 1, 1);
        widths[i] = spin_at(sizes, (int)i + 1, 1, a->width[i], MIN_SECTION_WIDTH, MAX_SECTION_WIDTH);
        minimal_widths[i] = spin_at(sizes, (int)i + 1, 2, a->minimal_width[i], MIN_SECTION_WIDTH, MAX_SECTION_WIDTH);
    }
    gtk_grid_attach(sizes, gtk_label_new("Warnung ab"), 1, 6, 1, 1);
    gtk_grid_attach(sizes, gtk_label_new("Kritisch ab"), 2, 6, 1, 1);
    const char *const thresholds[] = {"CPU (%)", "RAM (%)", "Temperatur (°C)"};
    GtkWidget *warn[3], *crit[3];
    for (unsigned i = 0; i < 3; ++i) {
        gtk_grid_attach(sizes, gtk_label_new(thresholds[i]), 0, 7 + (int)i, 1, 1);
        double maximum = i == SR_SECTION_TEMP ? 125 : 100;
        warn[i] = spin_at(sizes, 7 + (int)i, 1, a->display.warning[i], 0, maximum);
        crit[i] = spin_at(sizes, 7 + (int)i, 2, a->display.critical[i], 0, maximum);
    }

    /* Graph scales are independent from the warning/critical color limits.
     * RAM floor is deliberately in percent, not GiB: portable across machines
     * with 2 GiB, 8 GiB or 128 GiB of installed memory. */
    GtkWidget *scale_widget = settings_grid();
    GtkGrid *scale = GTK_GRID(scale_widget);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), scale_widget, gtk_label_new("Skalierung"));
    GtkWidget *floors[3];
    const char *const floor_labels[] = {
        "CPU – Untergrenze (%)", "RAM – Untergrenze (%)", "Temperatur – Untergrenze (°C)"
    };
    for (unsigned i = 0; i < 3; ++i) {
        GtkWidget *caption = gtk_label_new(floor_labels[i]);
        gtk_label_set_xalign(GTK_LABEL(caption), 0.0);
        gtk_grid_attach(scale, caption, 0, (int)i, 1, 1);
        floors[i] = spin_at(scale, (int)i, 1, a->display.floor[i],
                            i == SR_SECTION_TEMP ? -20 : 0, 95);
    }
    GtkWidget *scale_info = gtk_label_new(
        "CPU und RAM: Obergrenze automatisch 100 %.\n"
        "Temperatur: Obergrenze normalerweise 100 °C, bei Bedarf höher.\n"
        "RAM-Skala in Prozent, unabhängig von der verbauten Speichergröße.\n"
        "Untergrenzen beeinflussen nur die Verlaufsgrafik, nicht Messwerte\n"
        "oder die Warn- und Kritisch-Farben.");
    gtk_label_set_xalign(GTK_LABEL(scale_info), 0.0);
    gtk_grid_attach(scale, scale_info, 0, 4, 2, 1);

    xfce_panel_plugin_block_menu(plugin);
    xfce_panel_plugin_block_autohide(plugin, TRUE);
    xfce_panel_plugin_take_window(plugin, GTK_WINDOW(dialog));
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
            a->visible[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shows[i]));
            a->width[i] = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widths[i]));
            a->minimal_width[i] = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(minimal_widths[i]));
        }
        unsigned old_interval_ms = a->interval_ms;
        gboolean old_collect = a->display.graphs && !a->minimal;
        a->display.colors = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(colors));
        a->display.graphs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graphs));
        a->minimal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(minimal));
        a->interval_ms = (unsigned)lround(gtk_spin_button_get_value(GTK_SPIN_BUTTON(interval)) * 1000.0);
        a->interval_ms = CLAMP(a->interval_ms, MIN_INTERVAL_MS, MAX_INTERVAL_MS);
        a->display.points = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(points));
        for (unsigned i = 0; i < 3; ++i) {
            a->display.floor[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(floors[i]));
            a->display.warning[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(warn[i]));
            a->display.critical[i] = MAX(a->display.warning[i],
                                        gtk_spin_button_get_value(GTK_SPIN_BUTTON(crit[i])));
        }
        if (!old_collect && a->display.graphs && !a->minimal)
            memset(a->history, 0, sizeof(a->history));
        update_layout(a);
        if (old_interval_ms != a->interval_ms) reset_timer(a);
        save_settings(plugin, a);
        applet_tick(a);
    }
    gtk_widget_destroy(dialog);
    xfce_panel_plugin_block_autohide(plugin, FALSE);
    xfce_panel_plugin_unblock_menu(plugin);
}

static void mode_changed(XfcePanelPlugin *plugin, XfcePanelPluginMode mode, Applet *a) {
    (void)plugin;
    a->vertical = mode == XFCE_PANEL_PLUGIN_MODE_VERTICAL;
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        /* Cursor will be updated on next pointer motion; always reset on mode switch. */
        a->section[i].edge_hover = TRUE;
        set_edge_cursor(&a->section[i], FALSE, NULL);
    }
    update_layout(a);
}

static gboolean size_changed(XfcePanelPlugin *plugin, gint size, Applet *a) {
    (void)plugin; (void)size; (void)a;
    return TRUE;
}

static void free_data(XfcePanelPlugin *plugin, Applet *a) {
    (void)plugin;
    if (a->timeout_id) g_source_remove(a->timeout_id);
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i)
        if (a->section[i].layout) g_object_unref(a->section[i].layout);
    if (a->resize_h) g_object_unref(a->resize_h);
    if (a->resize_v) g_object_unref(a->resize_v);
    g_free(a);
}

static void construct(XfcePanelPlugin *plugin) {
    Applet *a = g_new0(Applet, 1);
    a->plugin = plugin;
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(plugin));
    a->resize_h = gdk_cursor_new_for_display(display, GDK_SB_H_DOUBLE_ARROW);
    a->resize_v = gdk_cursor_new_for_display(display, GDK_SB_V_DOUBLE_ARROW);
    sr_sampler_init(&a->sampler);
    read_settings(a);

    a->row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(plugin), a->row);
    for (unsigned i = 0; i < SR_SECTION_COUNT; ++i) {
        Section *s = &a->section[i];
        s->owner = a; s->index = i;
        s->event_box = gtk_event_box_new();
        /* Invisible input-only GdkWindow: captures events without painting a box. */
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(s->event_box), FALSE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(s->event_box), TRUE);
        gtk_widget_add_events(s->event_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        s->canvas = gtk_drawing_area_new();
        gtk_container_add(GTK_CONTAINER(s->event_box), s->canvas);
        s->layout = gtk_widget_create_pango_layout(s->canvas, "");
        pango_layout_set_single_paragraph_mode(s->layout, TRUE);
        g_signal_connect(s->canvas, "draw", G_CALLBACK(section_draw), s);
        g_signal_connect(s->event_box, "motion-notify-event", G_CALLBACK(section_motion), s);
        g_signal_connect(s->event_box, "enter-notify-event", G_CALLBACK(section_enter), s);
        g_signal_connect(s->event_box, "leave-notify-event", G_CALLBACK(section_leave), s);
        g_signal_connect(s->event_box, "button-press-event", G_CALLBACK(section_press), s);
        g_signal_connect(s->event_box, "button-release-event", G_CALLBACK(section_release), s);
        g_signal_connect(s->event_box, "grab-broken-event", G_CALLBACK(section_grab_broken), s);
        gtk_box_pack_start(GTK_BOX(a->row), s->event_box, FALSE, FALSE, 0);
        xfce_panel_plugin_add_action_widget(plugin, s->event_box);
    }
    a->empty_placeholder = gtk_event_box_new();
    GtkWidget *empty_label = gtk_label_new("SysRibbon (aus)");
    gtk_container_add(GTK_CONTAINER(a->empty_placeholder), empty_label);
    gtk_box_pack_start(GTK_BOX(a->row), a->empty_placeholder, FALSE, FALSE, 0);
    xfce_panel_plugin_add_action_widget(plugin, a->empty_placeholder);

    g_signal_connect(plugin, "configure-plugin", G_CALLBACK(options_dialog), a);
    g_signal_connect(plugin, "save", G_CALLBACK(save_settings), a);
    g_signal_connect(plugin, "mode-changed", G_CALLBACK(mode_changed), a);
    g_signal_connect(plugin, "size-changed", G_CALLBACK(size_changed), a);
    g_signal_connect(plugin, "free-data", G_CALLBACK(free_data), a);
    xfce_panel_plugin_menu_show_configure(plugin);
    xfce_panel_plugin_set_expand(plugin, FALSE);

    gtk_widget_show_all(a->row);
    mode_changed(plugin, xfce_panel_plugin_get_mode(plugin), a);
    applet_tick(a);
    reset_timer(a);
}

XFCE_PANEL_PLUGIN_REGISTER(construct);
