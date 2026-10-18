/* SPDX-License-Identifier: MIT */
/* Characterization tests of the real plugin's widgets, signals and settings.
 * Only the sampler boundary is replaced with deterministic demonstration data. */
#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4util/libxfce4util.h>
#include "../src/sampler.h"

XfcePanelPlugin *xfce_panel_module_construct(const gchar *, gint, const gchar *,
                                            const gchar *, gchar **, GdkScreen *);

typedef struct {
    GtkWidget *window;
    XfcePanelPlugin *plugin;
    GtkWidget *row;
    GtkWidget *sections[4];
    GtkWidget *placeholder;
    gint id;
} Panel;

static gint next_id;
static unsigned sample_step;
static gboolean sensors_available = TRUE;
static double cpu_value = 24.0;
static const char *screenshot_directory;

/* GNU ld --wrap keeps this fixture out of the installed plugin. */
void __wrap_sr_sample_selected(SrSampler *sampler, SrSample *sample, unsigned flags) {
    (void)sampler;
    static const double cpu_history[] = {12, 27, 43, 58, 36, 18, 32, 24};
    unsigned index = MIN(sample_step, G_N_ELEMENTS(cpu_history) - 1);
    ++sample_step;
    *sample = (SrSample){
        .cpu_ok = sensors_available && (flags & SR_CPU),
        .ram_ok = sensors_available && (flags & SR_RAM),
        .temp_ok = sensors_available && (flags & SR_TEMP),
        .network_ok = sensors_available && (flags & SR_NET),
        .cpu_pct = cpu_value == 24.0 ? cpu_history[index] : cpu_value,
        .ram_pct = 40.0, .ram_used_gib = 6.4, .ram_total_gib = 16.0,
        .temp_c = 48.0, .download_bps = 1.2 * 1048576, .upload_bps = 60 * 1024
    };
}

static void settle(unsigned milliseconds) {
    gint64 deadline = g_get_monotonic_time() + milliseconds * 1000;
    do {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);
}

static GtkWidget *find_type(GtkWidget *root, GType type) {
    if (g_type_is_a(G_OBJECT_TYPE(root), type)) return root;
    if (!GTK_IS_CONTAINER(root)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *item = children; item && !found; item = item->next)
        found = find_type(item->data, type);
    g_list_free(children);
    return found;
}

static GtkWidget *find_toggle(GtkWidget *root, const char *label) {
    if (GTK_IS_CHECK_BUTTON(root) &&
        g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)), label) == 0) return root;
    if (!GTK_IS_CONTAINER(root)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *item = children; item && !found; item = item->next)
        found = find_toggle(item->data, label);
    g_list_free(children);
    return found;
}

static GtkToggleButton *toggle(GtkWidget *dialog, const char *label) {
    GtkWidget *widget = find_toggle(dialog, label);
    g_assert_nonnull(widget);
    return GTK_TOGGLE_BUTTON(widget);
}

static GtkSpinButton *spin(GtkWidget *dialog, int page, int column, int row) {
    GtkNotebook *tabs = GTK_NOTEBOOK(find_type(dialog, GTK_TYPE_NOTEBOOK));
    g_assert_nonnull(tabs);
    GtkWidget *grid = gtk_notebook_get_nth_page(tabs, page);
    GtkWidget *widget = gtk_grid_get_child_at(GTK_GRID(grid), column, row);
    g_assert_true(GTK_IS_SPIN_BUTTON(widget));
    return GTK_SPIN_BUTTON(widget);
}

static void panel_open(Panel *panel, gint id) {
    panel->id = id;
    panel->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(panel->window), "SysRibbon panel test");
    gtk_window_set_decorated(GTK_WINDOW(panel->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(panel->window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(panel->window), 12);
    panel->plugin = xfce_panel_module_construct("sysribbon", id, "SysRibbon", "",
                                                NULL, gdk_screen_get_default());
    g_assert_nonnull(panel->plugin);
    gtk_widget_set_size_request(GTK_WIDGET(panel->plugin), -1, 28);
    gtk_container_add(GTK_CONTAINER(panel->window), GTK_WIDGET(panel->plugin));
    gtk_widget_show_all(panel->window);
    settle(80);
    panel->row = gtk_bin_get_child(GTK_BIN(panel->plugin));
    g_assert_true(GTK_IS_BOX(panel->row));
    GList *children = gtk_container_get_children(GTK_CONTAINER(panel->row));
    g_assert_cmpuint(g_list_length(children), ==, 5);
    for (unsigned i = 0; i < 4; ++i)
        panel->sections[i] = g_list_nth_data(children, i);
    panel->placeholder = g_list_nth_data(children, 4);
    g_list_free(children);
}

static void setup(Panel *panel, gconstpointer unused) {
    (void)unused;
    sensors_available = TRUE;
    cpu_value = 24.0;
    sample_step = 0;
    panel_open(panel, ++next_id);
}

static void teardown(Panel *panel, gconstpointer unused) {
    (void)unused;
    gtk_widget_destroy(panel->window);
    settle(20);
}

static int section_size(Panel *panel, unsigned index, gboolean vertical) {
    GtkWidget *canvas = gtk_bin_get_child(GTK_BIN(panel->sections[index]));
    return vertical ? gtk_widget_get_allocated_height(canvas) : gtk_widget_get_allocated_width(canvas);
}

static GdkPixbuf *capture(GtkWidget *widget) {
    settle(50);
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window(gtk_widget_get_window(widget),
                                                 0, 0, width, height);
    g_assert_nonnull(pixels);
    return pixels;
}

static unsigned color_pixels(GdkPixbuf *image, int red, int green, int blue) {
    unsigned count = 0;
    int channels = gdk_pixbuf_get_n_channels(image);
    int stride = gdk_pixbuf_get_rowstride(image);
    const guchar *pixels = gdk_pixbuf_read_pixels(image);
    for (int y = 0; y < gdk_pixbuf_get_height(image); ++y) {
        for (int x = 0; x < gdk_pixbuf_get_width(image); ++x) {
            const guchar *p = pixels + y * stride + x * channels;
            if (ABS((int)p[0] - red) <= 8 && ABS((int)p[1] - green) <= 8 &&
                ABS((int)p[2] - blue) <= 8) ++count;
        }
    }
    return count;
}

static void save_capture(GtkWidget *widget, const char *filename) {
    if (!screenshot_directory) return;
    GdkPixbuf *pixels = capture(widget);
    gchar *path = g_build_filename(screenshot_directory, filename, NULL);
    GError *error = NULL;
    gboolean saved = gdk_pixbuf_save(pixels, path, "png", &error, NULL);
    g_assert_no_error(error);
    g_assert_true(saved);
    g_test_message("Screenshot: %s", filename);
    g_free(path);
    g_object_unref(pixels);
}

typedef void (*DialogEdit)(GtkWidget *, gpointer);
typedef struct { DialogEdit edit; gpointer data; int response; } DialogAction;

static gboolean edit_dialog(gpointer data) {
    DialogAction *action = data;
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *item = windows; item; item = item->next)
        if (GTK_IS_DIALOG(item->data) && gtk_widget_get_visible(item->data)) dialog = item->data;
    g_list_free(windows);
    g_assert_nonnull(dialog);
    action->edit(dialog, action->data);
    GtkWidget *button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), action->response);
    g_assert_nonnull(button);
    gtk_button_clicked(GTK_BUTTON(button));
    return G_SOURCE_REMOVE;
}

static void configure(Panel *panel, DialogEdit edit, gpointer data, int response) {
    DialogAction action = {edit, data, response};
    g_idle_add(edit_dialog, &action);
    g_signal_emit_by_name(panel->plugin, "configure-plugin");
    settle(80);
}

static void assert_setting(Panel *panel, const char *name, int expected) {
    gchar *path = xfce_panel_plugin_save_location(panel->plugin, FALSE);
    g_assert_nonnull(path);
    XfceRc *rc = xfce_rc_simple_open(path, TRUE);
    g_assert_nonnull(rc);
    g_assert_cmpint(xfce_rc_read_int_entry(rc, name, -999), ==, expected);
    xfce_rc_close(rc);
    g_free(path);
}

static void test_default_display(Panel *panel, gconstpointer unused) {
    (void)unused;
    g_assert_false(gtk_widget_get_visible(panel->placeholder));
    const int expected[] = {162, 222, 172, 226};
    for (unsigned i = 0; i < 4; ++i) {
        g_assert_true(gtk_widget_get_mapped(panel->sections[i]));
        g_assert_cmpint(section_size(panel, i, FALSE), ==, expected[i]);
    }
    GdkPixbuf *image = capture(panel->window);
    g_assert_cmpuint(color_pixels(image, 80, 250, 123), >, 30);
    g_assert_cmpuint(color_pixels(image, 139, 233, 253), >, 30);
    g_object_unref(image);
}

static void edit_minimal(GtkWidget *dialog, gpointer unused) {
    (void)unused;
    GtkToggleButton *minimal = toggle(dialog, "Minimalmodus: nur Messwerte, ohne Bezeichnungen oder Verlauf");
    GtkWidget *graphs = GTK_WIDGET(toggle(dialog, "Verlaufsgrafiken anzeigen"));
    g_assert_true(gtk_widget_get_sensitive(graphs));
    gtk_toggle_button_set_active(minimal, TRUE);
    g_assert_false(gtk_widget_get_sensitive(graphs));
    gtk_toggle_button_set_active(minimal, FALSE);
    g_assert_true(gtk_widget_get_sensitive(graphs));
    gtk_toggle_button_set_active(minimal, TRUE);
}

static void test_minimal_persistence(Panel *panel, gconstpointer unused) {
    (void)unused;
    configure(panel, edit_minimal, NULL, GTK_RESPONSE_OK);
    const int expected[] = {68, 138, 82, 194};
    for (unsigned i = 0; i < 4; ++i)
        g_assert_cmpint(section_size(panel, i, FALSE), ==, expected[i]);
    gchar *path = xfce_panel_plugin_save_location(panel->plugin, FALSE);
    XfceRc *rc = xfce_rc_simple_open(path, TRUE);
    g_assert_nonnull(rc);
    g_assert_true(xfce_rc_read_bool_entry(rc, "minimal-mode", FALSE));
    xfce_rc_close(rc);
    g_free(path);
    gint id = panel->id;
    gtk_widget_destroy(panel->window);
    panel_open(panel, id);
    for (unsigned i = 0; i < 4; ++i)
        g_assert_cmpint(section_size(panel, i, FALSE), ==, expected[i]);
}

static void test_cancel(Panel *panel, gconstpointer unused) {
    (void)unused;
    configure(panel, edit_minimal, NULL, GTK_RESPONSE_CANCEL);
    g_assert_cmpint(section_size(panel, 0, FALSE), ==, 162);
    gchar *path = xfce_panel_plugin_save_location(panel->plugin, FALSE);
    g_assert_false(path && g_file_test(path, G_FILE_TEST_EXISTS));
    g_free(path);
}

static void edit_visibility(GtkWidget *dialog, gpointer data) {
    const char *labels[] = {"CPU anzeigen", "RAM anzeigen", "Temperatur anzeigen", "Netzwerk anzeigen"};
    for (unsigned i = 0; i < 4; ++i)
        gtk_toggle_button_set_active(toggle(dialog, labels[i]), GPOINTER_TO_INT(data));
}

static void test_empty_recovery(Panel *panel, gconstpointer unused) {
    (void)unused;
    configure(panel, edit_visibility, GINT_TO_POINTER(FALSE), GTK_RESPONSE_OK);
    for (unsigned i = 0; i < 4; ++i) g_assert_false(gtk_widget_get_visible(panel->sections[i]));
    g_assert_true(gtk_widget_get_mapped(panel->placeholder));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(panel->placeholder)))),
                    ==, "SysRibbon (aus)");
    configure(panel, edit_visibility, GINT_TO_POINTER(TRUE), GTK_RESPONSE_OK);
    for (unsigned i = 0; i < 4; ++i) g_assert_true(gtk_widget_get_mapped(panel->sections[i]));
    g_assert_false(gtk_widget_get_visible(panel->placeholder));
}

static void edit_settings(GtkWidget *dialog, gpointer unused) {
    (void)unused;
    gtk_spin_button_set_value(spin(dialog, 0, 1, 7), 0.1);
    gtk_spin_button_set_value(spin(dialog, 0, 1, 8), 12);
    gtk_spin_button_set_value(spin(dialog, 1, 1, 1), 240);
    gtk_spin_button_set_value(spin(dialog, 1, 2, 1), 96);
    gtk_spin_button_set_value(spin(dialog, 1, 1, 7), 75);
    gtk_spin_button_set_value(spin(dialog, 1, 2, 7), 60);
    gtk_spin_button_set_value(spin(dialog, 2, 1, 0), 10);
    gtk_spin_button_set_value(spin(dialog, 2, 1, 1), 20);
    gtk_spin_button_set_value(spin(dialog, 2, 1, 2), 35);
}

static void check_settings(GtkWidget *dialog, gpointer unused) {
    (void)unused;
    g_assert_cmpfloat_with_epsilon(gtk_spin_button_get_value(spin(dialog, 0, 1, 7)), 0.1, 0.001);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(spin(dialog, 1, 1, 1)), ==, 240);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(spin(dialog, 1, 2, 1)), ==, 96);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(spin(dialog, 1, 2, 7)), ==, 75);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(spin(dialog, 2, 1, 2)), ==, 35);
}

static void test_settings(Panel *panel, gconstpointer unused) {
    (void)unused;
    configure(panel, edit_settings, NULL, GTK_RESPONSE_OK);
    g_assert_cmpint(section_size(panel, 0, FALSE), ==, 240);
    assert_setting(panel, "interval-ms", 100);
    assert_setting(panel, "history-points", 12);
    assert_setting(panel, "width-cpu", 240);
    assert_setting(panel, "minimal-width-cpu", 96);
    assert_setting(panel, "cpu-warning", 75);
    assert_setting(panel, "cpu-critical", 75);
    assert_setting(panel, "cpu-floor-pct", 10);
    assert_setting(panel, "ram-floor-pct", 20);
    assert_setting(panel, "temp-floor-c", 35);
    gint id = panel->id;
    gtk_widget_destroy(panel->window);
    panel_open(panel, id);
    configure(panel, check_settings, NULL, GTK_RESPONSE_CANCEL);
}

static gboolean pointer_event(GtkWidget *widget, GdkEventType type,
                              double x, double y, double root_x, double root_y) {
    GdkEvent *event = gdk_event_new(type);
    event->any.window = g_object_ref(gtk_widget_get_window(widget));
    event->any.send_event = TRUE;
    GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(widget));
    gdk_event_set_device(event, gdk_seat_get_pointer(seat));
    if (type == GDK_MOTION_NOTIFY) {
        event->motion.x = x; event->motion.y = y;
        event->motion.x_root = root_x; event->motion.y_root = root_y;
        event->motion.state = GDK_BUTTON1_MASK;
    } else {
        event->button.button = GDK_BUTTON_PRIMARY;
        event->button.x = x; event->button.y = y;
        event->button.x_root = root_x; event->button.y_root = root_y;
    }
    gboolean handled = gtk_widget_event(widget, event);
    gdk_event_free(event);
    return handled;
}

static void test_resize(Panel *panel, gconstpointer unused) {
    (void)unused;
    GtkWidget *cpu = panel->sections[0];
    g_assert_false(pointer_event(cpu, GDK_BUTTON_PRESS, 20, 5, 100, 100));
    pointer_event(cpu, GDK_BUTTON_RELEASE, 70, 5, 150, 100);
    g_assert_cmpint(section_size(panel, 0, FALSE), ==, 162);
    g_assert_true(pointer_event(cpu, GDK_BUTTON_PRESS, 160, 5, 100, 100));
    g_assert_true(pointer_event(cpu, GDK_MOTION_NOTIFY, 190, 5, 130, 100));
    /* The release moves farther than the last motion event. */
    g_assert_true(pointer_event(cpu, GDK_BUTTON_RELEASE, 205, 5, 145, 100));
    settle(80);
    g_assert_cmpint(section_size(panel, 0, FALSE), ==, 207);
    assert_setting(panel, "width-cpu", 207);
    /* Clamp an oversized drag at the public 520px limit. */
    g_assert_true(pointer_event(cpu, GDK_BUTTON_PRESS, 205, 5, 100, 100));
    pointer_event(cpu, GDK_BUTTON_RELEASE, 1000, 5, 1000, 100);
    settle(80);
    g_assert_cmpint(section_size(panel, 0, FALSE), ==, 520);
    assert_setting(panel, "width-cpu", 520);
    g_assert_true(pointer_event(cpu, GDK_BUTTON_PRESS, 518, 5, 100, 100));
    pointer_event(cpu, GDK_BUTTON_RELEASE, -1000, 5, -1000, 100);
    settle(80);
    g_assert_cmpint(section_size(panel, 0, FALSE), ==, 42);
    assert_setting(panel, "width-cpu", 42);
}

static void test_vertical(Panel *panel, gconstpointer unused) {
    (void)unused;
    g_signal_emit_by_name(panel->plugin, "mode-changed", XFCE_PANEL_PLUGIN_MODE_VERTICAL);
    settle(80);
    g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(panel->row)), ==, GTK_ORIENTATION_VERTICAL);
    g_assert_cmpint(section_size(panel, 0, TRUE), ==, 162);
    GtkWidget *cpu = panel->sections[0];
    g_assert_true(pointer_event(cpu, GDK_BUTTON_PRESS, 5, 160, 100, 100));
    pointer_event(cpu, GDK_BUTTON_RELEASE, 5, 180, 100, 120);
    settle(80);
    g_assert_cmpint(section_size(panel, 0, TRUE), ==, 182);
    assert_setting(panel, "width-cpu", 182);
}

static void fast_interval(GtkWidget *dialog, gpointer unused) {
    (void)unused;
    gtk_spin_button_set_value(spin(dialog, 0, 1, 7), 0.1);
}

static void test_timer_and_missing_sensors(Panel *panel, gconstpointer unused) {
    (void)unused;
    configure(panel, fast_interval, NULL, GTK_RESPONSE_OK);
    cpu_value = 93.0;
    settle(160);
    GdkPixbuf *image = capture(panel->window);
    g_assert_cmpuint(color_pixels(image, 255, 85, 85), >, 10);
    g_object_unref(image);
    sensors_available = FALSE;
    settle(160);
    image = capture(panel->window);
    g_assert_cmpuint(color_pixels(image, 144, 144, 144), >, 10);
    g_assert_cmpuint(color_pixels(image, 255, 85, 85), ==, 0);
    g_object_unref(image);
}

static void edit_monochrome(GtkWidget *dialog, gpointer unused) {
    (void)unused;
    gtk_toggle_button_set_active(toggle(dialog, "Dynamische Farben verwenden"), FALSE);
    gtk_toggle_button_set_active(toggle(dialog, "Verlaufsgrafiken anzeigen"), FALSE);
}

static void test_monochrome_snapshot(Panel *panel, gconstpointer unused) {
    (void)unused;
    configure(panel, edit_monochrome, NULL, GTK_RESPONSE_OK);
    GdkPixbuf *image = capture(panel->window);
    g_assert_cmpuint(color_pixels(image, 80, 250, 123), ==, 0);
    g_assert_cmpuint(color_pixels(image, 139, 233, 253), ==, 0);
    g_object_unref(image);
    gchar *path = xfce_panel_plugin_save_location(panel->plugin, FALSE);
    XfceRc *rc = xfce_rc_simple_open(path, TRUE);
    g_assert_nonnull(rc);
    g_assert_false(xfce_rc_read_bool_entry(rc, "show-colors", TRUE));
    g_assert_false(xfce_rc_read_bool_entry(rc, "show-graphs", TRUE));
    xfce_rc_close(rc);
    g_free(path);
}

static void screenshot_settings(GtkWidget *dialog, gpointer unused) {
    (void)unused;
    GtkNotebook *tabs = GTK_NOTEBOOK(find_type(dialog, GTK_TYPE_NOTEBOOK));
    save_capture(dialog, "settings-display.png");
    gtk_notebook_set_current_page(tabs, 1);
    save_capture(dialog, "settings-widths.png");
    gtk_notebook_set_current_page(tabs, 2);
    save_capture(dialog, "settings-scaling.png");
}

static void test_screenshots(Panel *panel, gconstpointer unused) {
    (void)unused;
    if (!screenshot_directory) {
        g_test_skip("Use make screenshots to regenerate README images");
        return;
    }
    configure(panel, fast_interval, NULL, GTK_RESPONSE_OK);
    while (sample_step < 8) settle(5);
    save_capture(panel->window, "panel-default.png");
    configure(panel, screenshot_settings, NULL, GTK_RESPONSE_CANCEL);
    configure(panel, edit_minimal, NULL, GTK_RESPONSE_OK);
    save_capture(panel->window, "panel-minimal.png");
}

int main(int argc, char **argv) {
    if (argc == 3 && g_strcmp0(argv[1], "--screenshots") == 0) {
        screenshot_directory = argv[2];
        argc = 1;
    }
    /* The runner supplies a private configuration root before GTK/Xfce initialize. */
    const gchar *root = g_getenv("SYSRIBBON_GUI_TEST_ROOT");
    if (!root) g_error("Run this binary through tests/run_gui.py to isolate desktop settings");
    gchar *config = g_build_filename(root, "config", NULL);
    g_assert_cmpstr(g_getenv("XDG_CONFIG_HOME"), ==, config);
    g_free(config);
    gtk_test_init(&argc, &argv, NULL);
    const struct { const char *name; void (*run)(Panel *, gconstpointer); } tests[] = {
        {"default-display", test_default_display},
        {"minimal-persistence", test_minimal_persistence},
        {"cancel", test_cancel},
        {"empty-recovery", test_empty_recovery},
        {"settings", test_settings},
        {"resize", test_resize},
        {"vertical", test_vertical},
        {"timer-missing-sensors", test_timer_and_missing_sensors},
        {"monochrome-snapshot", test_monochrome_snapshot},
        {"screenshots", test_screenshots}
    };
    for (unsigned i = 0; i < G_N_ELEMENTS(tests); ++i) {
        gchar *name = g_strdup_printf("/gui/%s", tests[i].name);
        g_test_add(name, Panel, NULL, setup, tests[i].run, teardown);
        g_free(name);
    }
    return g_test_run();
}
