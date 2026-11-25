/*
 * Copyright © 2009-2018 Siyan Panayotov <contact@siyanpanayotov.com>
 *
 * This file is part of Viewnior.
 *
 * Viewnior is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Viewnior is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Viewnior.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libintl.h>
#include <glib/gi18n.h>
#define _(String) gettext (String)

#include "config.h"
#include "vnr-prefs.h"
#include "vnr-window.h"
#include "vnr-tools.h"

#define UI_PATH PACKAGE_DATA_DIR"/viewnior/vnr-preferences-dialog.ui"

G_DEFINE_TYPE (VnrPrefs, vnr_prefs, G_TYPE_OBJECT);

/**
 * VNR_PREFS_KEYS:
 *
 * Every preference that is saved in the config file, in the order they
 * are saved. Each entry is X(PK, PT, KN, DEF):
 * @PK: Name of the VnrPrefs member.
 * @PT: Type of the VnrPrefs member, a suffix for g_key_file_get_ and
 *      g_key_file_set_.
 * @KN: Name of the key in the key file.
 * @DEF: Default value for the preference.
 */
#define VNR_PREFS_KEYS(X) \
    X (zoom,              integer, "zoom-mode",         VNR_PREFS_ZOOM_SMART) \
    X (fit_on_fullscreen, boolean, "fit-on-fullscreen", TRUE) \
    X (show_hidden,       boolean, "show-hidden",       FALSE) \
    X (dark_background,   boolean, "dark-background",   FALSE) \
    X (smooth_images,     boolean, "smooth-images",     TRUE) \
    X (confirm_delete,    boolean, "confirm-delete",    TRUE) \
    X (reload_on_save,    boolean, "reload-on-save",    FALSE) \
    X (show_menu_bar,     boolean, "show-menu-bar",     FALSE) \
    X (show_toolbar,      boolean, "show-toolbar",      TRUE) \
    X (show_scrollbar,    boolean, "show-scrollbar",    TRUE) \
    X (show_statusbar,    boolean, "show-statusbar",    FALSE) \
    X (start_maximized,   boolean, "start-maximized",   FALSE) \
    X (slideshow_timeout, integer, "slideshow-timeout", 5) \
    X (auto_resize,       boolean, "auto-resize",       FALSE) \
    X (behavior_wheel,    integer, "behavior-wheel",    VNR_PREFS_WHEEL_ZOOM) \
    X (behavior_click,    integer, "behavior-click",    VNR_PREFS_CLICK_ZOOM) \
    X (behavior_modify,   integer, "behavior-modify",   VNR_PREFS_MODIFY_ASK) \
    X (jpeg_quality,      integer, "jpeg-quality",      90) \
    X (png_compression,   integer, "png-compression",   9) \
    X (desktop,           integer, "desktop",           VNR_PREFS_DESKTOP_AUTO)

#define VNR_PREF_SET_DEFAULT(PK, PT, KN, DEF)  prefs-> PK = DEF;
#define VNR_PREF_LOAD_KEY(PK, PT, KN, DEF)  prefs-> PK = g_key_file_get_ ## PT (conf, "prefs", KN, &read_error); if(read_error != NULL) { prefs-> PK = DEF; g_clear_error(&read_error); }
#define VNR_PREF_SAVE_KEY(PK, PT, KN, DEF)  g_key_file_set_ ## PT (conf, "prefs", KN, prefs-> PK);

/*************************************************************/
/***** Private signal handlers *******************************/
/*************************************************************/

/* Store the state of a check button in the preference @field */
static void
pref_toggled_cb (GtkToggleButton *togglebutton, gboolean *field)
{
    *field = gtk_toggle_button_get_active(togglebutton);
}

static void
change_zoom_mode_cb (GtkComboBox *widget, gpointer user_data)
{
    VNR_PREFS(user_data)->zoom = gtk_combo_box_get_active(widget);
    vnr_prefs_save(VNR_PREFS(user_data));
}

/* The desktops in the order the preferences dialog lists them, which
 * isn't the order of their values */
static const VnrPrefsDesktop desktop_order[] = {
    VNR_PREFS_DESKTOP_GNOME2,
    VNR_PREFS_DESKTOP_GNOME3,
    VNR_PREFS_DESKTOP_XFCE,
    VNR_PREFS_DESKTOP_LXDE,
    VNR_PREFS_DESKTOP_PUPPY,
    VNR_PREFS_DESKTOP_FLUXBOX,
    VNR_PREFS_DESKTOP_NITROGEN,
    VNR_PREFS_DESKTOP_MATE,
    VNR_PREFS_DESKTOP_CINNAMON,
    VNR_PREFS_DESKTOP_KDE,
    VNR_PREFS_DESKTOP_AUTO,
};

/* The position of @desktop in the dialog's list, or that of
 * Autodetect for a value no desktop has */
static gint
desktop_position (VnrPrefsDesktop desktop)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (desktop_order); i++)
        if (desktop_order[i] == desktop)
            return i;
    return desktop_position (VNR_PREFS_DESKTOP_AUTO);
}

static void
change_desktop_env_cb (GtkComboBox *widget, gpointer user_data)
{
    gint active = gtk_combo_box_get_active (widget);

    if (active < 0 || active >= (gint) G_N_ELEMENTS (desktop_order))
        return;
    VNR_PREFS(user_data)->desktop = desktop_order[active];
    vnr_prefs_save(VNR_PREFS(user_data));
}

static void
change_jpeg_quality_cb (GtkRange *range, gpointer user_data)
{
    VNR_PREFS(user_data)->jpeg_quality = (int) gtk_range_get_value(range);
    vnr_prefs_save(VNR_PREFS(user_data));
}

static void
change_png_compression_cb (GtkRange *range, gpointer user_data)
{
    VNR_PREFS(user_data)->png_compression = (int) gtk_range_get_value(range);
    vnr_prefs_save(VNR_PREFS(user_data));
}

static void
change_action_wheel_cb (GtkComboBox *widget, gpointer user_data)
{
    VNR_PREFS(user_data)->behavior_wheel = gtk_combo_box_get_active(widget);
    vnr_prefs_save(VNR_PREFS(user_data));
}

static void
change_action_click_cb (GtkComboBox *widget, gpointer user_data)
{
    VNR_PREFS(user_data)->behavior_click = gtk_combo_box_get_active(widget);
    vnr_prefs_save(VNR_PREFS(user_data));
}

static void
change_action_modify_cb (GtkComboBox *widget, gpointer user_data)
{
    VNR_PREFS(user_data)->behavior_modify = gtk_combo_box_get_active(widget);
    vnr_prefs_save(VNR_PREFS(user_data));
}

static void
change_spin_value_cb (GtkSpinButton *spinbutton, gpointer user_data)
{
    int new_value;

    new_value = gtk_spin_button_get_value_as_int (spinbutton);

    VNR_PREFS(user_data)->slideshow_timeout = new_value;
    vnr_prefs_save(VNR_PREFS(user_data));
    vnr_window_apply_preferences(VNR_WINDOW(VNR_PREFS(user_data)->vnr_win));
}

/*************************************************************/
/***** Private actions ***************************************/
/*************************************************************/

static void
vnr_prefs_set_default(VnrPrefs *prefs)
{
    VNR_PREFS_KEYS (VNR_PREF_SET_DEFAULT)

    /* Set from the command line, not saved */
    prefs->start_slideshow = FALSE;
    prefs->start_fullscreen = FALSE;
}

/* Make the check button @id show and change the preference @field. With
 * @apply, changes are applied to the window right away. */
static void
bind_toggle (GtkBuilder *builder, const gchar *id, VnrPrefs *prefs,
             gboolean *field, gboolean apply)
{
    GtkToggleButton *button = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, id));

    gtk_toggle_button_set_active (button, *field);

    /* Handlers run in the order they are connected: store, save, apply */
    g_signal_connect (button, "toggled", G_CALLBACK (pref_toggled_cb), field);
    g_signal_connect_swapped (button, "toggled", G_CALLBACK (vnr_prefs_save), prefs);
    if (apply)
        g_signal_connect_swapped (button, "toggled",
                                  G_CALLBACK (vnr_window_apply_preferences),
                                  prefs->vnr_win);
}

/* Create a combo box with the NULL-terminated @items, showing item
 * @active, that calls @callback with @prefs when the choice changes */
static GtkWidget *
combo_new (const gchar *const *items, gint active, GCallback callback, VnrPrefs *prefs)
{
    GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT (gtk_combo_box_text_new ());

    for (; *items != NULL; items++)
        gtk_combo_box_text_append_text (combo, *items);
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active);
    gtk_widget_show (GTK_WIDGET (combo));
    g_signal_connect (combo, "changed", callback, prefs);

    return GTK_WIDGET (combo);
}

static GtkWidget *
build_dialog (VnrPrefs *prefs)
{
    GtkBuilder *builder;
    GtkWidget *window;
    GError *error = NULL;

    GObject *close_button;
    GtkSpinButton *slideshow_timeout;
    GtkGrid *behavior_table;
    GtkRange *jpeg_scale;
    GtkRange *png_scale;

    const gchar *zoom_modes[] = {
        _("Smart Mode"), _("1:1 Mode"), _("Fit To Window Mode"), _("Last Used Mode"), NULL
    };
    const gchar *desktops[] = {
        "GNOME 2", "GNOME 3", "XFCE", "LXDE", "PUPPY", "FluxBox", "Nitrogen", "MATE",
        "Cinnamon", "KDE Plasma", _("Autodetect"), NULL
    };
    const gchar *wheel_actions[] = {
        _("Navigate images"), _("Zoom image"), _("Scroll image up/down"), NULL
    };
    const gchar *click_actions[] = {
        _("Switch zoom modes"), _("Enter fullscreen mode"), _("Navigate images"), NULL
    };
    const gchar *modify_actions[] = {
        _("Ask every time"), _("Autosave"), _("Ignore changes"), NULL
    };

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, UI_PATH, &error);

    if (error != NULL)
    {
        g_warning ("%s\n", error->message);
        g_object_unref(builder);
        return NULL;
    }

    window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));

    /* Close button */
    close_button = gtk_builder_get_object (builder, "close_button");
    g_signal_connect_swapped(close_button, "clicked",
                             G_CALLBACK(gtk_widget_hide_on_delete), window);

    /* Checkboxes */
    bind_toggle (builder, "show_hidden", prefs, &prefs->show_hidden, FALSE);
    bind_toggle (builder, "dark_background", prefs, &prefs->dark_background, TRUE);
    bind_toggle (builder, "fit_on_fullscreen", prefs, &prefs->fit_on_fullscreen, FALSE);
    bind_toggle (builder, "smooth_images", prefs, &prefs->smooth_images, TRUE);
    bind_toggle (builder, "confirm_delete", prefs, &prefs->confirm_delete, FALSE);
    bind_toggle (builder, "reload", prefs, &prefs->reload_on_save, FALSE);

    /* Slideshow timeout spin button */
    slideshow_timeout = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "slideshow_timeout"));
    gtk_spin_button_set_value( slideshow_timeout, (gdouble)prefs->slideshow_timeout);
    prefs->slideshow_timeout_widget = slideshow_timeout;
    g_signal_connect(G_OBJECT(slideshow_timeout), "value-changed", G_CALLBACK(change_spin_value_cb), prefs);

    /* JPEG quality scale */
    jpeg_scale = GTK_RANGE (gtk_builder_get_object (builder, "jpeg_scale"));
    gtk_range_set_value(jpeg_scale, (gdouble)prefs->jpeg_quality);
    g_signal_connect(G_OBJECT(jpeg_scale), "value-changed", G_CALLBACK(change_jpeg_quality_cb), prefs);

    /* PNG compression scale */
    png_scale = GTK_RANGE (gtk_builder_get_object (builder, "png_scale"));
    gtk_range_set_value(png_scale, (gdouble)prefs->png_compression);
    g_signal_connect(G_OBJECT(png_scale), "value-changed", G_CALLBACK(change_png_compression_cb), prefs);

    /* Zoom mode combo box */
    gtk_box_pack_end (GTK_BOX (gtk_builder_get_object (builder, "zoom_mode_box")),
                      combo_new (zoom_modes, prefs->zoom,
                                 G_CALLBACK (change_zoom_mode_cb), prefs),
                      FALSE, FALSE, 0);

    /* Desktop combo box */
    gtk_box_pack_end (GTK_BOX (gtk_builder_get_object (builder, "desktop_box")),
                      combo_new (desktops, desktop_position (prefs->desktop),
                                 G_CALLBACK (change_desktop_env_cb), prefs),
                      FALSE, FALSE, 0);

    /* Behavior combo boxes */
    behavior_table = GTK_GRID (gtk_builder_get_object (builder, "behavior_table"));

    gtk_grid_attach (behavior_table,
                     combo_new (wheel_actions, prefs->behavior_wheel,
                                G_CALLBACK (change_action_wheel_cb), prefs),
                     1, 0, 1, 1);
    gtk_grid_attach (behavior_table,
                     combo_new (click_actions, prefs->behavior_click,
                                G_CALLBACK (change_action_click_cb), prefs),
                     1, 1, 1, 1);
    gtk_grid_attach (behavior_table,
                     combo_new (modify_actions, prefs->behavior_modify,
                                G_CALLBACK (change_action_modify_cb), prefs),
                     1, 2, 1, 1);

    /* Window signals */
    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    g_signal_connect(G_OBJECT(window), "key-press-event",
                     G_CALLBACK(vnr_tools_hide_on_escape_cb), NULL);


    g_object_unref (G_OBJECT (builder));

    return window;
}

static gboolean
vnr_prefs_load (VnrPrefs *prefs, GError **error)
{
    GKeyFile *conf = g_key_file_new();
    GError *read_error = NULL;
    GError *load_file_error = NULL;
    const gchar *path;

    path = vnr_prefs_get_config_path ("viewnior.conf");
    g_key_file_load_from_file (conf, path, G_KEY_FILE_NONE, &load_file_error);

    g_free((char*)path);

    if(load_file_error != NULL)
    {
        g_propagate_error (error, load_file_error);
        g_key_file_free (conf);
        return FALSE;
    }

    VNR_PREFS_KEYS (VNR_PREF_LOAD_KEY)

    g_key_file_free (conf);

    return TRUE;
}

/*************************************************************/
/***** Stuff that deals with the type ************************/
/*************************************************************/

static void
vnr_prefs_class_init (VnrPrefsClass * klass) {}

GObject *
vnr_prefs_new (GtkWidget *vnr_win)
{
    VnrPrefs *prefs;

    prefs = g_object_new (VNR_TYPE_PREFS, NULL);

    prefs->vnr_win = vnr_win;

    return (GObject *) prefs;
}

static void
vnr_prefs_init (VnrPrefs * prefs)
{
    GError *error = NULL;

    if ( !vnr_prefs_load (prefs, &error) )
    {
        g_warning("Error loading config file: %s. All preferences are set to their default values. Saving ...", error->message);
        vnr_prefs_set_default(prefs);
        vnr_prefs_save (prefs);
    }

    prefs->dialog = NULL;
}

/*************************************************************/
/***** Actions ***********************************************/
/*************************************************************/

/* The path of the file @name in Viewnior's config directory. Free it with
 * g_free(). */
gchar *
vnr_prefs_get_config_path (const gchar *name)
{
    return g_build_filename (g_get_user_config_dir(), PACKAGE, name, NULL);
}

void
vnr_prefs_show_dialog(VnrPrefs *prefs)
{
    if (prefs->dialog == NULL)
    {
        prefs->dialog = build_dialog (prefs);
        if (prefs->dialog == NULL)
            return;
    }
    gtk_window_present(GTK_WINDOW(prefs->dialog));
}

gboolean
vnr_prefs_save (VnrPrefs *prefs)
{
    GKeyFile *conf;
    FILE *rcfile;
    const gchar *dir;
    const gchar *path;

    path = vnr_prefs_get_config_path ("viewnior.conf");
    dir = g_path_get_dirname (path);

    conf = g_key_file_new();
    VNR_PREFS_KEYS (VNR_PREF_SAVE_KEY)

    if(g_mkdir_with_parents (dir, 0700) != 0)
        g_warning("Error creating config file's parent directory (%s)\n", dir);

    rcfile = fopen(path , "w");

    if(rcfile != NULL)
    {
        gchar *data = g_key_file_to_data (conf, NULL, NULL);
        fputs(data, rcfile);
        fclose(rcfile);
        g_free(data);
    }
    else
        g_warning("Saving config file: Unable to open the configuration file for writing!\n");

    g_key_file_free (conf);
    g_free((char*)dir);
    g_free((char*)path);

    return TRUE;
}

void
vnr_prefs_set_slideshow_timeout (VnrPrefs *prefs, int value)
{
    if (prefs->dialog != NULL)
        gtk_spin_button_set_value(prefs->slideshow_timeout_widget, (gdouble)value);
}

static void
vnr_prefs_set_bool (VnrPrefs *prefs, gboolean *field, gboolean value)
{
    if(*field != value)
    {
        *field = value;
        vnr_prefs_save(prefs);
    }
}

void
vnr_prefs_set_show_toolbar (VnrPrefs *prefs, gboolean show_toolbar)
{
    vnr_prefs_set_bool (prefs, &prefs->show_toolbar, show_toolbar);
}

void
vnr_prefs_set_show_scrollbar (VnrPrefs *prefs, gboolean show_scrollbar)
{
    vnr_prefs_set_bool (prefs, &prefs->show_scrollbar, show_scrollbar);
}

void
vnr_prefs_set_show_statusbar (VnrPrefs *prefs, gboolean show_statusbar)
{
    vnr_prefs_set_bool (prefs, &prefs->show_statusbar, show_statusbar);
}

void
vnr_prefs_set_show_menu_bar (VnrPrefs *prefs, gboolean show_menu_bar)
{
    vnr_prefs_set_bool (prefs, &prefs->show_menu_bar, show_menu_bar);
}
