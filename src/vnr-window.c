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

#include <stdlib.h>
#include <string.h>
#include <libintl.h>
#include <glib/gi18n.h>
#define _(String) gettext (String)

#include "config.h"
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "vnr-window.h"
#include "uni-scroll-win.h"
#include "uni-anim-view.h"
#include "vnr-tools.h"
#include "vnr-file.h"
#include "vnr-message-area.h"
#include "vnr-properties-dialog.h"
#include "vnr-crop.h"
#include "uni-exiv2.hpp"
#include "uni-utils.h"

/* Timeout to hide the toolbar in fullscreen mode */
#define FULLSCREEN_TIMEOUT 1000
#define DARK_BACKGROUND_COLOR "#222222"

G_DEFINE_TYPE (VnrWindow, vnr_window, GTK_TYPE_WINDOW);

static void vnr_window_unfullscreen (VnrWindow *window);
static void stop_slideshow(VnrWindow *window);
static void start_slideshow(VnrWindow *window);
static void restart_slideshow(VnrWindow *window);
static void allow_slideshow(VnrWindow *window);
static gint get_top_widgets_height(VnrWindow *window);

static void leave_fs_cb (GtkButton *button, VnrWindow *window);
static void toggle_show_next_cb (GtkToggleButton *togglebutton, VnrWindow *window);
static void spin_value_change_cb (GtkSpinButton *spinbutton, VnrWindow *window);
static void save_image_cb (GtkWidget *widget, VnrWindow *window);
static void zoom_changed_cb (UniImageView *view, VnrWindow *window);
static gboolean fullscreen_timeout_cb (VnrWindow *window);
static gboolean leave_image_area_cb(GtkWidget * widget, GdkEventCrossing * ev, VnrWindow *window);
static gboolean fullscreen_motion_cb(GtkWidget * widget, GdkEventMotion * ev, VnrWindow *window);
static void open_with_launch_application_cb (GtkAction *action, VnrWindow *window);

const gchar *ui_definition = "<ui>"
  "<menubar name=\"MainMenu\">"
    "<menu action=\"File\">"
      "<menuitem action=\"FileOpen\"/>"
      "<menuitem action=\"FileOpenDir\"/>"
      "<menu action=\"FileOpenWith\">"
         "<placeholder name=\"AppEntries\"/>"
      "</menu>"
      "<separator/>"
      "<menuitem action=\"FileSave\"/>"
      "<menuitem action=\"FileReload\"/>"
      "<separator/>"
      "<menuitem action=\"FileProperties\"/>"
      "<separator/>"
      "<menuitem action=\"FileClose\"/>"
    "</menu>"
    "<menu action=\"Edit\">"
      "<menuitem action=\"FileDelete\"/>"
      "<menuitem action=\"FilePermanentDelete\"/>"
      "<separator/>"
      "<menuitem action=\"EditPreferences\"/>"
    "</menu>"
    "<menu action=\"View\">"
      "<menuitem action=\"ViewMenuBar\"/>"
      "<menuitem action=\"ViewToolbar\"/>"
      "<menuitem action=\"ViewScrollbar\"/>"
      "<menuitem action=\"ViewStatusbar\"/>"
      "<separator/>"
      "<menuitem action=\"ViewZoomIn\"/>"
      "<menuitem action=\"ViewZoomOut\"/>"
      "<menuitem action=\"ViewZoomNormal\"/>"
      "<menuitem action=\"ViewZoomFit\"/>"
      "<menuitem action=\"ViewZoom50\"/>"
      "<separator/>"
      "<menuitem name=\"Fullscreen\" action=\"ViewFullscreen\"/>"
      "<menuitem name=\"Slideshow\" action=\"ViewSlideshow\"/>"
      "<separator/>"
      "<menuitem name=\"ResizeWindow\" action=\"ViewResizeWindow\"/>"
    "</menu>"
    "<menu action=\"Image\">"
      "<menuitem action=\"ImageFlipVertical\"/>"
      "<menuitem action=\"ImageFlipHorizontal\"/>"
      "<separator/>"
      "<menuitem action=\"ImageRotateCW\"/>"
      "<menuitem action=\"ImageRotateCCW\"/>"
      "<separator/>"
      "<menuitem action=\"ImageCrop\"/>"
      "<placeholder name=\"WallpaperEntry\"/>"
      "<placeholder name=\"CopyEntry\"/>"
    "</menu>"
    "<menu action=\"Go\">"
      "<menuitem name=\"GoPrevious\" action=\"GoPrevious\"/>"
      "<menuitem name=\"GoNext\" action=\"GoNext\"/>"
      "<separator/>"
      "<menuitem name=\"GoFirst\" action=\"GoFirst\"/>"
      "<menuitem name=\"GoLast\" action=\"GoLast\"/>"
    "</menu>"
    "<menu action=\"Help\">"
      "<menuitem action=\"HelpAbout\"/>"
    "</menu>"
  "</menubar>"
  "<popup name=\"ButtonMenu\">"
    "<menuitem action=\"FileOpen\"/>"
    "<menuitem action=\"FileOpenDir\"/>"
    "<menu action=\"FileOpenWith\">"
       "<placeholder name=\"AppEntries\"/>"
    "</menu>"
    "<separator/>"
    "<menuitem action=\"FileSave\"/>"
    "<menuitem action=\"FileReload\"/>"
    "<menuitem action=\"FileProperties\"/>"
    "<separator/>"
    "<menu action=\"Edit\">"
      "<menuitem action=\"FileDelete\"/>"
      "<menuitem action=\"FilePermanentDelete\"/>"
      "<separator/>"
    "</menu>"
    "<menu action=\"View\">"
      "<menuitem action=\"ViewZoomIn\"/>"
      "<menuitem action=\"ViewZoomOut\"/>"
      "<menuitem action=\"ViewZoomNormal\"/>"
      "<menuitem action=\"ViewZoomFit\"/>"
      "<menuitem action=\"ViewZoom50\"/>"
      "<separator/>"
      "<menuitem action=\"ViewMenuBar\"/>"
      "<menuitem action=\"ViewToolbar\"/>"
      "<menuitem action=\"ViewScrollbar\"/>"
      "<menuitem action=\"ViewStatusbar\"/>"
      "<menuitem name=\"Fullscreen\" action=\"ViewFullscreen\"/>"
      "<menuitem name=\"Slideshow\" action=\"ViewSlideshow\"/>"
      "<separator/>"
      "<menuitem name=\"ResizeWindow\" action=\"ViewResizeWindow\"/>"
    "</menu>"
    "<menu action=\"Image\">"
      "<menuitem action=\"ImageFlipVertical\"/>"
      "<menuitem action=\"ImageFlipHorizontal\"/>"
      "<separator/>"
      "<menuitem action=\"ImageRotateCW\"/>"
      "<menuitem action=\"ImageRotateCCW\"/>"
      "<separator/>"
      "<menuitem action=\"ImageCrop\"/>"
      "<placeholder name=\"WallpaperEntry\"/>"
      "<placeholder name=\"CopyEntry\"/>"
    "</menu>"
    "<separator/>"
    "<menuitem action=\"EditPreferences\"/>"
    "<separator/>"
    "<menuitem action=\"HelpAbout\"/>"
    "<menuitem action=\"FileClose\"/>"
  "</popup>"
  "<toolbar name=\"Toolbar\">"
    "<toolitem action=\"GoPrevious\"/>"
    "<toolitem action=\"GoNext\"/>"
    "<separator/>"
    "<toolitem action=\"ViewZoomIn\"/>"
    "<toolitem action=\"ViewZoomOut\"/>"
    "<toolitem action=\"ViewZoomNormal\"/>"
    "<toolitem action=\"ViewZoomFit\"/>"
    "<separator/>"
    "<toolitem action=\"ImageRotateCCW\"/>"
    "<toolitem action=\"ImageRotateCW\"/>"
    "<separator expand=\"true\"/>"
    "<toolitem action=\"Properties\"/>"
  "</toolbar>"
  "<popup name=\"PopupMenu\">"
    "<menuitem name=\"GoPrevious\" action=\"GoPrevious\"/>"
    "<menuitem name=\"GoNext\" action=\"GoNext\"/>"
    "<separator/>"
    "<menu action=\"FileOpenWith\">"
       "<placeholder name=\"AppEntries\"/>"
    "</menu>"
    "<separator/>"
    "<menuitem action=\"ViewZoomIn\"/>"
    "<menuitem action=\"ViewZoomOut\"/>"
    "<menuitem action=\"ViewZoomNormal\"/>"
    "<menuitem action=\"ViewZoomFit\"/>"
    "<menuitem action=\"ViewZoom50\"/>"
    "<placeholder name=\"WallpaperEntry\"/>"
    "<placeholder name=\"CopyEntry\"/>"
    "<separator/>"
    "<menuitem name=\"MenuBar\" action=\"ViewMenuBar\"/>"
    "<menuitem name=\"Toolbar\" action=\"ViewToolbar\"/>"
    "<menuitem name=\"Scrollbar\" action=\"ViewScrollbar\"/>"
    "<menuitem name=\"Statusbar\" action=\"ViewStatusbar\"/>"
    "<menuitem name=\"Fullscreen\" action=\"ViewFullscreen\"/>"
    "<separator/>"
    "<menuitem action=\"FileProperties\"/>"
  "</popup>"
  "<accelerator name=\"ControlEqualAccel\" action=\"ControlEqual\"/>"
  "<accelerator name=\"ControlKPAddAccel\" action=\"ControlKpAdd\"/>"
  "<accelerator name=\"ControlKPSubAccel\" action=\"ControlKpSub\"/>"
  "<accelerator name=\"DeleteAccel\" action=\"Delete\"/>"
  "<accelerator name=\"PermanentDeleteAccel\" action=\"PermanentDelete\"/>"
"</ui>";


const gchar *ui_definition_wallpaper = "<ui>"
  "<menubar name=\"MainMenu\">"
    "<menu action=\"Image\">"
      "<placeholder name=\"WallpaperEntry\">"
        "<separator/>"
        "<menuitem name=\"Wallpaper\" action=\"SetAsWallpaper\"/>"
      "</placeholder>"
    "</menu>"
  "</menubar>"
  "<popup name=\"ButtonMenu\">"
    "<menu action=\"Image\">"
      "<placeholder name=\"WallpaperEntry\">"
        "<separator/>"
        "<menuitem name=\"Wallpaper\" action=\"SetAsWallpaper\"/>"
      "</placeholder>"
    "</menu>"
  "</popup>"
  "<popup name=\"PopupMenu\">"
    "<placeholder name=\"WallpaperEntry\">"
      "<separator/>"
      "<menuitem action=\"SetAsWallpaper\"/>"
    "</placeholder>"
  "</popup>"
"</ui>";

const gchar *ui_definition_copy = "<ui>"
  "<menubar name=\"MainMenu\">"
    "<menu action=\"Image\">"
      "<placeholder name=\"CopyEntry\">"
        "<separator/>"
        "<menuitem name=\"Copy\" action=\"CopyImage\"/>"
      "</placeholder>"
    "</menu>"
  "</menubar>"
  "<popup name=\"ButtonMenu\">"
    "<menu action=\"Image\">"
      "<placeholder name=\"CopyEntry\">"
        "<separator/>"
        "<menuitem name=\"Copy\" action=\"CopyImage\"/>"
      "</placeholder>"
    "</menu>"
  "</popup>"
  "<popup name=\"PopupMenu\">"
    "<placeholder name=\"CopyEntry\">"
      "<separator/>"
      "<menuitem action=\"CopyImage\"/>"
    "</placeholder>"
  "</popup>"
"</ui>";

/*************************************************************/
/***** Private actions ***************************************/
/*************************************************************/
/* Modified version of eog's eog_window_update_openwith_menu */
static void
vnr_window_update_openwith_menu (VnrWindow *window)
{
    GFile *file;
    GFileInfo *file_info;
    GList *iter;
    gchar *label, *tip;
    const gchar *mime_type;
    GtkAction *action;
    GList *apps;
    guint action_id = 0;
    guint i;
    static const gchar *const menu_paths[] = {
        "/MainMenu/File/FileOpenWith/AppEntries",
        "/ButtonMenu/FileOpenWith/AppEntries",
        "/PopupMenu/FileOpenWith/AppEntries",
    };

    file = g_file_new_for_path ((gchar*)VNR_FILE(window->file_list->data)->path);
    file_info = g_file_query_info (file,
                       G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                       0, NULL, NULL);

    if (file_info == NULL)
        return;
    else
        mime_type = g_file_info_get_content_type (file_info);

    if (window->open_with_menu_id != 0)
    {
           gtk_ui_manager_remove_ui (window->ui_mngr, window->open_with_menu_id);
           window->open_with_menu_id = 0;
    }

    if (window->actions_open_with != NULL)
    {
          gtk_ui_manager_remove_action_group (window->ui_mngr, window->actions_open_with);
          window->actions_open_with = NULL;
    }

    if (mime_type == NULL)
    {
            g_object_unref (file_info);
            return;
    }

    apps = g_app_info_get_all_for_type (mime_type);

    g_object_unref (file_info);

    if (!apps)
            return;

    window->actions_open_with = gtk_action_group_new ("OpenWithActions");
    gtk_ui_manager_insert_action_group (window->ui_mngr, window->actions_open_with, -1);

    window->open_with_menu_id = gtk_ui_manager_new_merge_id (window->ui_mngr);

    for (iter = apps; iter; iter = iter->next) {
        GAppInfo *app = iter->data;
        gchar name[64];

        /* Do not include viewnior itself */
        if (g_ascii_strcasecmp (g_app_info_get_executable (app),
                                g_get_prgname ()) == 0)
        {
                g_object_unref (app);
                continue;
        }

        g_snprintf (name, sizeof (name), "OpenWith%u", action_id++);

        label = g_strdup (g_app_info_get_name (app));
        tip = g_strdup_printf (_("Use \"%s\" to open the selected image"), g_app_info_get_name (app));
        action = gtk_action_new (name, label, tip, NULL);

        g_free (label);
        g_free (tip);

        g_object_set_data_full (G_OBJECT (action), "app", app,
                                (GDestroyNotify) g_object_unref);

        g_signal_connect (action,
                          "activate",
                          G_CALLBACK (open_with_launch_application_cb),
                          window);

        gtk_action_group_add_action (window->actions_open_with, action);
        g_object_unref (action);

        for (i = 0; i < G_N_ELEMENTS (menu_paths); i++)
            gtk_ui_manager_add_ui (window->ui_mngr,
                            window->open_with_menu_id,
                            menu_paths[i],
                            name,
                            name,
                            GTK_UI_MANAGER_MENUITEM,
                            FALSE);
    }

    g_list_free (apps);
}

static void
vnr_window_save_accel_map()
{
    gchar *accelfile = vnr_prefs_get_config_path ("accel_map");

    gtk_accel_map_save (accelfile);
    g_free (accelfile);
}

static void
vnr_window_load_accel_map()
{
    gchar *accelfile = vnr_prefs_get_config_path ("accel_map");

    gtk_accel_map_load (accelfile);
    g_free (accelfile);
}

/* Set the background color of the image view, or reset it to the
 * theme's when color is NULL. */
static void
vnr_window_set_view_background(VnrWindow *window, const char *color)
{
    GtkCssProvider *provider;

    provider = g_object_get_data (G_OBJECT (window->view), "vnr-bg-provider");
    if (provider == NULL)
    {
        provider = gtk_css_provider_new ();
        gtk_style_context_add_provider (gtk_widget_get_style_context (window->view),
                                        GTK_STYLE_PROVIDER (provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_set_data_full (G_OBJECT (window->view), "vnr-bg-provider",
                                provider, g_object_unref);
    }

    if (color != NULL)
    {
        gchar *css = g_strdup_printf ("* { background-color: %s; }", color);
        gtk_css_provider_load_from_data (provider, css, -1, NULL);
        g_free (css);
    }
    else
    {
        gtk_css_provider_load_from_data (provider, "", -1, NULL);
    }
}

static void
vnr_window_hide_cursor(VnrWindow *window)
{
    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    GdkCursor *cursor = gdk_cursor_new_for_display(gdk_window_get_display(gdk_window),
                                                   GDK_BLANK_CURSOR);
    gdk_window_set_cursor (gdk_window, cursor);
    g_object_unref (cursor);
    window->cursor_is_hidden = TRUE;
}

static void
vnr_window_show_cursor(VnrWindow *window)
{
    gdk_window_set_cursor (gtk_widget_get_window(GTK_WIDGET(window)), NULL);
    window->cursor_is_hidden = FALSE;
}

/* Show a busy cursor on the widget's window during a long-running
 * operation, or restore the default one. */
static void
vnr_window_set_busy_cursor(GtkWidget *widget, gboolean busy)
{
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (gdk_window == NULL)
        return;

    if (busy)
    {
        GdkDisplay *display = gdk_window_get_display(gdk_window);
        GdkCursor *cursor = gdk_cursor_new_from_name(display, "wait");
        gdk_window_set_cursor(gdk_window, cursor);
        if (cursor != NULL)
            g_object_unref(cursor);
        /* This makes the cursor show NOW */
        gdk_display_flush(display);
    }
    else
    {
        gdk_window_set_cursor(gdk_window, NULL);
    }
}

/* Like vnr_window_set_busy_cursor() on the main window, but leaves a
 * cursor hidden in fullscreen alone. */
static void
vnr_window_set_busy(VnrWindow *window, gboolean busy)
{
    if(!window->cursor_is_hidden)
        vnr_window_set_busy_cursor(GTK_WIDGET(window), busy);
}

static void
update_fs_filename_label(VnrWindow *window)
{
    if(window->mode == VNR_WINDOW_MODE_NORMAL)
        return;

    gint position, total;
    char *buf;

    get_position_of_element_in_list(window->file_list, &position, &total);
    buf = g_strdup_printf ("%s - %i/%i",
                           VNR_FILE(window->file_list->data)->display_name,
                           position, total);

    gtk_label_set_text(GTK_LABEL(window->fs_filename_label), buf);

    g_free(buf);
}

static gboolean next_image_src(VnrWindow *window);

/* Show the next image of the slideshow after the timeout */
static void
slideshow_schedule_next(VnrWindow *window)
{
    window->ss_source_tag = g_timeout_add_seconds (window->ss_timeout,
                                                   (GSourceFunc)next_image_src,
                                                   window);
}

static gboolean
next_image_src(VnrWindow *window)
{
    if(g_list_length(g_list_first(window->file_list)) <= 1)
        return FALSE;
    else
        vnr_window_next(window, FALSE);

    slideshow_schedule_next(window);

    return FALSE;
}

static void
fullscreen_unset_timeout(VnrWindow *window)
{
    if(window->fs_source != NULL)
    {
        g_source_unref (window->fs_source);
        g_source_destroy (window->fs_source);
        window->fs_source = NULL;
    }
}

static void
fullscreen_set_timeout(VnrWindow *window)
{
    fullscreen_unset_timeout(window);

    window->fs_source = g_timeout_source_new (FULLSCREEN_TIMEOUT);
    g_source_set_callback (window->fs_source,
                           (GSourceFunc)fullscreen_timeout_cb,
                           window, NULL);

    g_source_attach (window->fs_source, NULL);
}

static GtkWidget *
get_fs_controls(VnrWindow *window)
{
    if(window->fs_controls != NULL)
        return window->fs_controls;

    GtkWidget *box;
    GtkToolItem *item;
    GtkWidget *widget;
    GtkAdjustment *spinner_adj;

    /* Tool item, that contains the hbox */
    item = gtk_tool_item_new();
    gtk_tool_item_set_expand(item, TRUE);

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (item), box);

    widget = gtk_button_new_with_mnemonic(VNR_GTK_LABEL ("_Leave Fullscreen"));
    gtk_button_set_image(GTK_BUTTON(widget),
                         gtk_image_new_from_icon_name("view-restore", GTK_ICON_SIZE_BUTTON));
    g_signal_connect(widget, "clicked", G_CALLBACK(leave_fs_cb), window);
    gtk_box_pack_end (GTK_BOX(box), widget, FALSE, FALSE, 0);

    /* Create label for the current image's filename */
    widget = gtk_label_new(NULL);
    gtk_label_set_ellipsize (GTK_LABEL(widget), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable (GTK_LABEL(widget), TRUE);
    window->fs_filename_label = widget;
    gtk_box_pack_end (GTK_BOX(box), widget, TRUE, TRUE, 10);


    widget = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start (GTK_BOX(box), widget, FALSE, FALSE, 0);

    widget = gtk_check_button_new_with_label(_("Show next image after: "));
    g_signal_connect (widget, "toggled", G_CALLBACK(toggle_show_next_cb),
                      window);
    gtk_box_pack_start (GTK_BOX(box), widget, FALSE, FALSE, 0);
    window->toggle_btn = widget;

    /* Create spin button to adjust slideshow's timeout */
    //spinner_adj = (GtkAdjustment *) gtk_adjustment_new (5, 1.0, 30.0, 1.0, 1.0, 0);
    spinner_adj = (GtkAdjustment *) gtk_adjustment_new (window->prefs->slideshow_timeout, 1.0, 30.0, 1.0, 1.0, 0);
    widget = gtk_spin_button_new (spinner_adj, 1.0, 0);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON(widget), TRUE);
    gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON(widget),
                                       GTK_UPDATE_ALWAYS);
    g_signal_connect (widget, "value-changed",
                      G_CALLBACK(spin_value_change_cb), window);
    gtk_box_pack_start (GTK_BOX(box), widget, FALSE, FALSE, 0);
    window->ss_timeout_widget = widget;

    window->fs_seconds_label = gtk_label_new(ngettext(" second", " seconds", 5));
    gtk_box_pack_start (GTK_BOX(box), window->fs_seconds_label, FALSE, FALSE, 0);

    window->fs_controls = GTK_WIDGET(item);

    gtk_widget_show_all (window->fs_controls);
    return window->fs_controls;
}

static void
vnr_window_set_drag(VnrWindow *window)
{
    gtk_drag_dest_set (GTK_WIDGET (window),
                       GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
                       NULL, 0,
                       GDK_ACTION_COPY | GDK_ACTION_ASK);
    gtk_drag_dest_add_uri_targets (GTK_WIDGET (window));
}

static void
vnr_window_fullscreen(VnrWindow *window)
{
    GtkAction *action;

    gtk_widget_hide(window->menu_bar);
    gtk_window_fullscreen(GTK_WINDOW(window));

    window->mode = VNR_WINDOW_MODE_FULLSCREEN;
    action = gtk_action_group_get_action (window->actions_image,
                                          "ViewFullscreen");

    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
    vnr_window_set_view_background(window, "black");

    if (window->prefs->fit_on_fullscreen)
        uni_image_view_set_zoom_mode (UNI_IMAGE_VIEW(window->view),
                                      VNR_PREFS_ZOOM_FIT);

    update_fs_filename_label(window);
    gtk_widget_hide (window->toolbar);
    gtk_widget_hide (window->statusbar);

    if (window->prefs->show_menu_bar)
        gtk_widget_show (window->properties_button);

    gtk_widget_show (window->fs_controls);

    stop_slideshow(window);

    /* Reset timeouts for the toolbar autohide when the mouse
     * moves over the UniImageviewer.
     * "after" because it must be called after the uniImageView's
     * callback (when the image is dragged).*/
    g_signal_connect_after (window->view,
                            "motion-notify-event",
                            G_CALLBACK (fullscreen_motion_cb),
                            window);

    /* Never hide the toolbar, while the mouse is over it */
    g_signal_connect (window->toolbar,
                      "enter-notify-event",
                      G_CALLBACK (leave_image_area_cb),
                      window);

    g_signal_connect (window->msg_area,
                      "enter-notify-event",
                      G_CALLBACK (leave_image_area_cb),
                      window);

    fullscreen_set_timeout(window);
}

static void
vnr_window_unfullscreen(VnrWindow *window)
{
    if(window->mode == VNR_WINDOW_MODE_NORMAL)
        return;

    GtkAction *action;

    stop_slideshow(window);
    window->mode = VNR_WINDOW_MODE_NORMAL;

    gtk_widget_show(window->menu_bar);
    gtk_window_unfullscreen(GTK_WINDOW(window));
    action = gtk_action_group_get_action (window->actions_image,
                                          "ViewFullscreen");

    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);

    vnr_window_set_view_background(window,
                                   window->prefs->dark_background ? DARK_BACKGROUND_COLOR : NULL);

    if (window->prefs->fit_on_fullscreen)
        uni_image_view_set_zoom_mode (UNI_IMAGE_VIEW(window->view),
                                      window->prefs->zoom);

    if(window->prefs->show_menu_bar)
        gtk_widget_hide (window->properties_button);
    else
        gtk_widget_hide (window->menu_bar);

    gtk_widget_hide (window->fs_controls);

    if(!window->prefs->show_toolbar)
        gtk_widget_hide (window->toolbar);
    else
        gtk_widget_show (window->toolbar);

    if(!window->prefs->show_statusbar)
        gtk_widget_hide (window->statusbar);
    else
        gtk_widget_show (window->statusbar);

    g_signal_handlers_disconnect_by_func(window->view,
                                         G_CALLBACK(fullscreen_motion_cb),
                                         window);

    g_signal_handlers_disconnect_by_func(window->toolbar,
                                         G_CALLBACK(leave_image_area_cb),
                                         window);

    g_signal_handlers_disconnect_by_func(window->msg_area,
                                         G_CALLBACK(leave_image_area_cb),
                                         window);

    fullscreen_unset_timeout(window);
    vnr_window_show_cursor(window);
}

/* Set the "Show next image" button and the Slideshow menu item without
 * their handlers starting or stopping the slideshow again. */
static void
slideshow_sync_toggles(VnrWindow *window, gboolean active)
{
    GtkAction *action;

    action = gtk_action_group_get_action (window->actions_collection,
                                          "ViewSlideshow");

    window->slideshow = FALSE;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(window->toggle_btn), active);
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), active);
    window->slideshow = TRUE;
}

static void
stop_slideshow(VnrWindow *window)
{
    if(!window->slideshow)
        return;

    if(window->mode != VNR_WINDOW_MODE_SLIDESHOW)
        return;

    slideshow_sync_toggles(window, FALSE);

    window->mode = VNR_WINDOW_MODE_FULLSCREEN;

    g_source_remove (window->ss_source_tag);
}

static void
start_slideshow(VnrWindow *window)
{
    if(!window->slideshow)
        return;

    if(window->mode == VNR_WINDOW_MODE_SLIDESHOW)
        return;

    window->mode = VNR_WINDOW_MODE_SLIDESHOW;

    slideshow_schedule_next(window);
    slideshow_sync_toggles(window, TRUE);
}

static void
restart_slideshow(VnrWindow *window)
{
    if(!window->slideshow)
        return;

    if(window->mode != VNR_WINDOW_MODE_SLIDESHOW)
        return;

    g_source_remove (window->ss_source_tag);
    slideshow_schedule_next(window);
}

static void
allow_slideshow(VnrWindow *window)
{
    if(window->slideshow)
        return;

    window->slideshow = TRUE;

    gtk_widget_set_sensitive(window->toggle_btn, TRUE);
}

static gint
get_top_widgets_height(VnrWindow *window)
{
    GtkAllocation allocation;

    if (!window->prefs->show_menu_bar && !window->prefs->show_toolbar)
    {
        return 0;
    }

    gtk_widget_get_allocation (window->menus, &allocation);

    return allocation.height;
}

void
deny_slideshow(VnrWindow *window)
{
    if(!window->slideshow)
        return;

    window->slideshow = FALSE;

    gtk_widget_set_sensitive(window->toggle_btn, FALSE);
}

/* Close the image, show @message as an error and turn off moving between
 * images and the slideshow. vnr_window_set_list() turns them back on for
 * a list of more than one image. */
static void
vnr_window_show_load_error(VnrWindow *window, const char *message)
{
    gtk_action_group_set_sensitive(window->actions_collection, FALSE);
    deny_slideshow(window);
    /* This also closes the image */
    vnr_message_area_show(VNR_MESSAGE_AREA (window->msg_area), TRUE,
                          message, TRUE);
}

/* Show @result, an edited copy of the current image, in its place.
 * Takes ownership of @result, which is NULL when the edit ran out of
 * memory. Returns FALSE in that case. */
static gboolean
vnr_window_show_edited(VnrWindow *window, GdkPixbuf *result)
{
    if(result == NULL)
    {
        vnr_message_area_show(VNR_MESSAGE_AREA(window->msg_area),
                              TRUE, _("Not enough virtual memory."),
                              FALSE);
        return FALSE;
    }

    uni_anim_view_set_static(UNI_ANIM_VIEW(window->view), result);

    window->current_image_width = gdk_pixbuf_get_width (result);
    window->current_image_height = gdk_pixbuf_get_height (result);
    g_object_unref(result);

    if(gtk_widget_get_visible(window->props_dlg))
        vnr_properties_dialog_update_image(VNR_PROPERTIES_DIALOG(window->props_dlg));

    return TRUE;
}

/* Call after changing window->modifications. Depending on the
 * preferences, offers to save the edits, saves them right away or says
 * that they can't be saved. */
static void
vnr_window_image_modified(VnrWindow *window)
{
    gtk_action_group_set_sensitive(window->action_save,
                                   window->modifications && window->writable_format_name != NULL);

    /* The edits cancelled each other out */
    if(window->modifications == 0)
    {
        vnr_message_area_hide(VNR_MESSAGE_AREA(window->msg_area));
        return;
    }

    if(window->writable_format_name == NULL)
        vnr_message_area_show(VNR_MESSAGE_AREA(window->msg_area),
                              TRUE,
                              _("Image modifications cannot be saved.\nWriting in this format is not supported."),
                              FALSE);
    else if(window->prefs->behavior_modify == VNR_PREFS_MODIFY_SAVE)
        save_image_cb(NULL, window);
    else if(window->prefs->behavior_modify == VNR_PREFS_MODIFY_ASK)
        vnr_message_area_show_with_button(VNR_MESSAGE_AREA(window->msg_area),
                                          FALSE,
                                          _("Save modifications?\nThis will overwrite the image and may reduce its quality!"),
                                          FALSE, _("_Save"),
                                          G_CALLBACK(save_image_cb));
}

static void
rotate_pixbuf(VnrWindow *window, GdkPixbufRotation angle)
{
    GdkPixbuf *result;

    /* Stop slideshow while editing the image */
    stop_slideshow(window);

    vnr_window_set_busy(window, TRUE);
    result = gdk_pixbuf_rotate_simple(UNI_IMAGE_VIEW(window->view)->pixbuf,
                                      angle);
    vnr_window_set_busy(window, FALSE);

    if(!vnr_window_show_edited(window, result))
        return;

    /* Extra conditions. Rotating 180 degrees is also flipping horizontal and vertical */
    if((window->modifications & (4))^((angle==GDK_PIXBUF_ROTATE_CLOCKWISE)<<2))
        window->modifications ^= 3;

    window->modifications ^= 4;
    vnr_window_image_modified(window);
}

static void
flip_pixbuf(VnrWindow *window, gboolean horizontal)
{
    GdkPixbuf *result;

    vnr_window_set_busy(window, TRUE);
    result = gdk_pixbuf_flip(UNI_IMAGE_VIEW(window->view)->pixbuf,
                             horizontal);
    vnr_window_set_busy(window, FALSE);

    if(!vnr_window_show_edited(window, result))
        return;

    /* Extra conditions. Rotating 180 degrees is also flipping horizontal and vertical */
    window->modifications ^= (window->modifications&4)?1+horizontal:2-horizontal;
    vnr_window_image_modified(window);
}

/*************************************************************/
/***** Private signal handlers *******************************/
/*************************************************************/
/* Modified version of eog's open_with_launch_application_cb */
static void
open_with_launch_application_cb (GtkAction *action, VnrWindow *window)
{
    GAppInfo *app;
    GFile *file;
    GList *files = NULL;

    file = g_file_new_for_path ((gchar*)VNR_FILE(window->file_list->data)->path);

    app = g_object_get_data (G_OBJECT (action), "app");
    files = g_list_append (files, file);
    g_app_info_launch (app,
               files,
               NULL, NULL);

    g_object_unref (file);
    g_list_free (files);
}

static gboolean
leave_image_area_cb(GtkWidget * widget, GdkEventCrossing * ev, VnrWindow *window)
{
    fullscreen_unset_timeout (window);
    return FALSE;
}

static gboolean
fullscreen_motion_cb(GtkWidget * widget, GdkEventMotion * ev, VnrWindow *window)
{
    if(window->disable_autohide)
        return FALSE;

    /* Show the toolbar only when the mouse moves to the top
     * of the UniImageView */
    if (ev->y < 20 && !gtk_widget_get_visible (window->toolbar))
        gtk_widget_show (GTK_WIDGET (window->toolbar));

    if(window->cursor_is_hidden)
        vnr_window_show_cursor(window);

    fullscreen_set_timeout(window);
    return FALSE;
}

/* Hides the toolbar */
static gboolean
fullscreen_timeout_cb (VnrWindow *window)
{
    fullscreen_unset_timeout (window);

    if(window->disable_autohide)
        return FALSE;

    gtk_widget_hide (window->toolbar);
    vnr_window_hide_cursor(window);
    return FALSE;
}

static void
spin_value_change_cb (GtkSpinButton *spinbutton, VnrWindow *window)
{
    int new_value = gtk_spin_button_get_value_as_int (spinbutton);

    if(new_value != window->prefs->slideshow_timeout)
        vnr_prefs_set_slideshow_timeout(window->prefs, new_value);

    gtk_label_set_text (GTK_LABEL(window->fs_seconds_label),
                        ngettext(" second", " seconds", new_value));
    window->ss_timeout = new_value;
    restart_slideshow(window);
}

static void
toggle_show_next_cb (GtkToggleButton *togglebutton, VnrWindow *window)
{
    if(!window->slideshow)
        return;

    if(window->mode == VNR_WINDOW_MODE_FULLSCREEN)
        start_slideshow(window);
    else if(window->mode == VNR_WINDOW_MODE_SLIDESHOW)
        stop_slideshow(window);
}

static void
save_image_cb (GtkWidget *widget, VnrWindow *window)
{
    GError *error = NULL;
    gboolean saved;

    if(window->writable_format_name == NULL)
    {
        vnr_message_area_show(VNR_MESSAGE_AREA(window->msg_area),
                              TRUE,
                              _("Image modifications cannot be saved.\nWriting in this format is not supported."),
                              FALSE);
        return;
    }

    vnr_window_set_busy(window, TRUE);

    if(window->prefs->behavior_modify == VNR_PREFS_MODIFY_ASK)
        vnr_message_area_hide(VNR_MESSAGE_AREA(window->msg_area));

    /* Store exiv2 metadata to cache, so we can restore it afterwards */
    uni_read_exiv2_to_cache(VNR_FILE(window->file_list->data)->path);

    if(g_strcmp0(window->writable_format_name, "jpeg" ) == 0)
    {
        gchar *quality;
        quality = g_strdup_printf ("%i", window->prefs->jpeg_quality);

        saved = gdk_pixbuf_save (uni_image_view_get_pixbuf(UNI_IMAGE_VIEW(window->view)),
                         VNR_FILE(window->file_list->data)->path, "jpeg",
                         &error, "quality", quality, NULL);
        g_free(quality);
    }
    else if(g_strcmp0(window->writable_format_name, "png" ) == 0)
    {
        gchar *compression;
        compression = g_strdup_printf ("%i", window->prefs->png_compression);

        saved = gdk_pixbuf_save (uni_image_view_get_pixbuf(UNI_IMAGE_VIEW(window->view)),
                         VNR_FILE(window->file_list->data)->path, "png",
                         &error, "compression", compression, NULL);
        g_free(compression);
    }
    else
    {
        saved = gdk_pixbuf_save (uni_image_view_get_pixbuf(UNI_IMAGE_VIEW(window->view)),
                         VNR_FILE(window->file_list->data)->path,
                         window->writable_format_name, &error, NULL);
    }
    /* Only onto the new pixels: the old ones still need the original
     * metadata. The pixels have the orientation applied already, if
     * gdk-pixbuf found one: don't let the copied tag rotate them again. */
    if(saved)
        uni_write_exiv2_from_cache(VNR_FILE(window->file_list->data)->path,
                                   window->orientation_applied);

    vnr_window_set_busy(window, FALSE);

    if(error != NULL)
    {
        vnr_message_area_show(VNR_MESSAGE_AREA(window->msg_area), TRUE,
                                               error->message, FALSE);
        return;
    }

    if(window->prefs->reload_on_save)
    {
        vnr_window_open(window, FALSE);
        return;
    }

    window->modifications = 0;

    gtk_action_group_set_sensitive(window->action_save, FALSE);

    if(window->prefs->behavior_modify != VNR_PREFS_MODIFY_ASK)
        zoom_changed_cb(UNI_IMAGE_VIEW(window->view), window);

    if(gtk_widget_get_visible(window->props_dlg))
        vnr_properties_dialog_update(VNR_PROPERTIES_DIALOG(window->props_dlg));
}

static void
vnr_window_cmd_open_menu (GtkToggleAction *action, VnrWindow *window)
{
    if( !gtk_toggle_action_get_active (action)) {
        return;
    }
    gtk_menu_popup_at_widget(GTK_MENU(window->button_menu),
                             window->properties_button,
                             GDK_GRAVITY_SOUTH_EAST,
                             GDK_GRAVITY_NORTH_EAST,
                             NULL);
    return;
}

static void
vnr_window_cmd_main_menu_hidden (GtkWidget *widget, gpointer user_data)
{
    gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(VNR_WINDOW(user_data)->properties_button), FALSE);
}

static void
leave_fs_cb (GtkButton *button, VnrWindow *window)
{
    vnr_window_unfullscreen (window);
}

static void
window_realize_cb(GtkWidget *widget, gpointer user_data)
{
    g_signal_handlers_disconnect_by_func(widget, window_realize_cb, user_data);

    if(!vnr_message_area_is_critical(VNR_MESSAGE_AREA(VNR_WINDOW(widget)->msg_area)))
    {
        if ( VNR_WINDOW(widget)->prefs->start_maximized ) {
            vnr_window_open(VNR_WINDOW(widget), FALSE);
        }
        else
        {
            GdkRectangle monitor;
            gdk_monitor_get_geometry (gdk_display_get_monitor_at_window (gtk_widget_get_display (widget),
                                                                         gtk_widget_get_window (widget)),
                                      &monitor);

            VNR_WINDOW(widget)->max_width = monitor.width * 0.9 - 100;
            VNR_WINDOW(widget)->max_height = monitor.height * 0.9 - 100;

            vnr_window_open(VNR_WINDOW(widget), TRUE);
        }
        if ( VNR_WINDOW(widget)->prefs->start_slideshow && VNR_WINDOW(widget)->file_list != NULL ) {
            vnr_window_fullscreen(VNR_WINDOW(widget));
            VNR_WINDOW(widget)->mode = VNR_WINDOW_MODE_NORMAL;
            allow_slideshow(VNR_WINDOW(widget));
            start_slideshow(VNR_WINDOW(widget));
        } else if ( VNR_WINDOW(widget)->prefs->start_fullscreen && VNR_WINDOW(widget)->file_list != NULL ) {
            vnr_window_fullscreen(VNR_WINDOW(widget));
        }
    }
}

static gboolean
window_change_state_cb (GtkWidget * widget, GdkEventWindowState * event, gpointer user_data)
{
    if ( event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED ) {
        /* Detect maximized state only */
        if ( event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED  ) {
            VNR_WINDOW(widget)->prefs->start_maximized = TRUE;
        } else {
            VNR_WINDOW(widget)->prefs->start_maximized = FALSE;
        }
        vnr_prefs_save(VNR_WINDOW(widget)->prefs);
    }
    return TRUE;
}


static void
window_destroy_cb (GtkWidget *widget, gpointer user_data)
{
    vnr_window_save_accel_map();
    vnr_prefs_save(VNR_WINDOW(widget)->prefs);
    gtk_main_quit();
}

static void
zoom_changed_cb (UniImageView *view, VnrWindow *window)
{
    gint position, total;
    char *buf = NULL;

    /* Change the info, only if there is an image
     * (vnr_window_close isn't called on the current image) */
    if(gtk_action_group_get_sensitive (window->actions_image))
    {
        get_position_of_element_in_list(window->file_list, &position, &total);
        buf = g_strdup_printf ("%s%s - %i/%i - %ix%i - %i%%", (window->modifications)?"*":"",
                               VNR_FILE(window->file_list->data)->display_name,
                               position, total,
                               window->current_image_width, window->current_image_height,
                               (int)(view->zoom*100.));

        gtk_window_set_title (GTK_WINDOW(window), buf);

        gint context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(window->statusbar), "statusbar");
        gtk_statusbar_pop (GTK_STATUSBAR(window->statusbar), GPOINTER_TO_INT(context_id));
        gtk_statusbar_push(GTK_STATUSBAR(window->statusbar), GPOINTER_TO_INT(context_id), buf);

        g_free(buf);
    }
}


static void
window_drag_begin_cb (GtkWidget *widget,
              GdkDragContext *drag_context,
              GtkSelectionData *data,
              guint info,
              guint time,
              gpointer user_data)
{
    gchar *uris[2];

    uris[0] = g_filename_to_uri((gchar*)VNR_FILE(VNR_WINDOW(user_data)->file_list->data)->path, NULL, NULL);
    uris[1] = NULL;

    gtk_selection_data_set_uris (data, uris);

    g_free(uris[0]);
}

static void
file_open_dialog_response_cb (GtkWidget *dialog,
                              gint response_id,
                              VnrWindow *window)
{
    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        GSList *uri_list = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (dialog));
        g_return_if_fail(uri_list != NULL);
        vnr_window_open_from_list(window, uri_list);
        g_slist_free_full(uri_list, g_free);
    }

    gtk_widget_destroy (dialog);
}

static void
vnr_window_cmd_preferences(GtkAction *action, gpointer user_data)
{
    vnr_prefs_show_dialog(VNR_WINDOW(user_data)->prefs);
}

static void
vnr_window_cmd_flip_horizontal(GtkAction *action, VnrWindow *window)
{
    if ( !gtk_action_group_get_sensitive(window->actions_static_image) )
        return;

    flip_pixbuf(window, TRUE);
}

static void
vnr_window_cmd_flip_vertical(GtkAction *action, VnrWindow *window)
{
    if ( !gtk_action_group_get_sensitive(window->actions_static_image) )
        return;

    flip_pixbuf(window, FALSE);
}

static void
vnr_window_cmd_rotate_cw(GtkAction *action, gpointer user_data)
{
    rotate_pixbuf(VNR_WINDOW(user_data), GDK_PIXBUF_ROTATE_CLOCKWISE);
}

static void
vnr_window_cmd_rotate_ccw(GtkAction *action, gpointer user_data)
{
    rotate_pixbuf(VNR_WINDOW(user_data), GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
}

static void
vnr_window_cmd_zoom_in (GtkAction *action, gpointer user_data)
{
    uni_image_view_zoom_in(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view));
}

static void
vnr_window_cmd_zoom_out (GtkAction *action, gpointer user_data)
{
    uni_image_view_zoom_out(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view));
}

static void
vnr_window_cmd_zoom_50 (GtkAction *action, gpointer user_data)
{
    uni_image_view_set_zoom(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view), 0.5);
    uni_image_view_set_fitting(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view), UNI_FITTING_NONE);
}

static void
vnr_window_cmd_normal_size (GtkAction *action, gpointer user_data)
{
    uni_image_view_set_zoom(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view), 1);
    uni_image_view_set_fitting(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view), UNI_FITTING_NONE);
}

static void
vnr_window_cmd_fit (GtkAction *action, gpointer user_data)
{
    uni_image_view_set_fitting(UNI_IMAGE_VIEW(VNR_WINDOW(user_data)->view), UNI_FITTING_FULL);
}

static void
vnr_window_cmd_next (GtkAction *action, gpointer user_data)
{
    vnr_window_next(VNR_WINDOW(user_data), TRUE);
}

static void
vnr_window_cmd_first (GtkAction *action, gpointer user_data)
{
    vnr_window_first(VNR_WINDOW(user_data));
}

static void
vnr_window_cmd_last (GtkAction *action, gpointer user_data)
{
    vnr_window_last(VNR_WINDOW(user_data));
}

static void
vnr_window_cmd_prev (GtkAction *action, gpointer user_data)
{
    vnr_window_prev(VNR_WINDOW(user_data));
}

static void
vnr_window_resize (VnrWindow *window)
{
    gint img_w = window->current_image_width;          /* Width of the pixbuf */
    gint img_h = window->current_image_height;         /* Height of the pixbuf */

    vnr_tools_fit_to_size (&img_w, &img_h, window->max_width, window->max_height);

    gtk_window_resize (GTK_WINDOW (window), img_w, img_h + get_top_widgets_height(window));
}

static void
vnr_window_cmd_resize (GtkToggleAction *action, VnrWindow *window)
{
    if ( action != NULL && !gtk_toggle_action_get_active(action) ) {
        window->prefs->auto_resize = FALSE;
        return;
    }

    if ( window->current_image_width == 0 || window->current_image_height == 0 )
        return;

    window->prefs->auto_resize = TRUE;

    vnr_window_resize (window);
}

static void
vnr_window_cmd_properties (GtkAction *action, VnrWindow *window)
{
    vnr_properties_dialog_show(VNR_PROPERTIES_DIALOG (window->props_dlg));
}

static void
vnr_window_cmd_reload (GtkAction *action, VnrWindow *window)
{
    vnr_window_open(window, FALSE);
}


static gboolean
file_size_is_small(char *filename) {

    struct stat st;
    int four_mb = 4 * 1024 * 1024;

    if(filename != NULL && stat(filename, &st) == 0) {
        return st.st_size < four_mb;
    }
    return FALSE;
}

static void
update_preview_cb(GtkFileChooser *file_chooser, gpointer data)
{
    GtkWidget *preview = GTK_WIDGET(data);
    char *filename = gtk_file_chooser_get_preview_filename(file_chooser);
    gboolean has_preview = FALSE;

    if(file_size_is_small(filename)) {
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_size(filename, 256, 256, NULL);
        has_preview = pixbuf != NULL;

        gtk_image_set_from_pixbuf(GTK_IMAGE(preview), pixbuf);
        if(pixbuf) {
            g_object_unref(pixbuf);
        }
    }
    gtk_file_chooser_set_preview_widget_active(file_chooser, has_preview);
    if(filename != NULL) {
        g_free(filename);
    }
}

/* Show a dialog for opening images, or folders if @folder is TRUE */
static void
vnr_window_show_open_dialog(VnrWindow *window, gboolean folder)
{
    GtkWidget *dialog;

    dialog = gtk_file_chooser_dialog_new (folder ? _("Open Folder") : _("Open Image"),
                          GTK_WINDOW(window),
                          folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                 : GTK_FILE_CHOOSER_ACTION_OPEN,
                          VNR_GTK_LABEL ("_Cancel"), GTK_RESPONSE_CANCEL,
                          VNR_GTK_LABEL ("_Open"), GTK_RESPONSE_ACCEPT,
                          NULL);

    gtk_window_set_modal (GTK_WINDOW(dialog), FALSE);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    if(!folder)
    {
        GtkWidget *preview;
        GtkFileFilter *img_filter;
        GtkFileFilter *all_filter;

        img_filter = gtk_file_filter_new ();
        g_assert (img_filter != NULL);
        gtk_file_filter_add_pixbuf_formats (img_filter);
        gtk_file_filter_add_mime_type (img_filter, "image/vnd.microsoft.icon");
        gtk_file_filter_set_name (img_filter, _("All Images"));
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), img_filter);

        all_filter = gtk_file_filter_new ();
        g_assert(all_filter != NULL);
        gtk_file_filter_add_pattern (all_filter, "*");
        gtk_file_filter_set_name (all_filter, _("All Files"));
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), all_filter);

        gtk_file_chooser_set_filter (GTK_FILE_CHOOSER(dialog), img_filter);

        preview = gtk_image_new();
        gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(dialog), preview);
        g_signal_connect(GTK_FILE_CHOOSER(dialog), "update-preview",
                         G_CALLBACK(update_preview_cb), preview);
    }

    if(window->file_list != NULL)
    {
        gchar *dirname = g_path_get_dirname (VNR_FILE(window->file_list->data)->path);
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), dirname);
        g_free(dirname);
    }

    g_signal_connect (dialog, "response",
                      G_CALLBACK (file_open_dialog_response_cb),
                      window);

    gtk_widget_show_all (GTK_WIDGET(dialog));

    /* This only works when here. */
    gtk_file_chooser_set_show_hidden (GTK_FILE_CHOOSER(dialog), window->prefs->show_hidden);
}

static void
vnr_window_cmd_open(GtkAction *action, VnrWindow *window)
{
    vnr_window_show_open_dialog(window, FALSE);
}

static void
vnr_window_cmd_open_dir(GtkAction *action, VnrWindow *window)
{
    vnr_window_show_open_dialog(window, TRUE);
}

static void
vnr_window_cmd_about (GtkAction *action, VnrWindow *window)
{
    static const char *authors[] = {
        "Programming &amp; icon design",
        "\tSiyan Panayotov <contact@siyanpanayotov.com>",
        "\nRefer to source code from GtkImageView",
        NULL
    };

    char *license =
          ("Viewnior is free software: you can redistribute it and/or modify "
           "it under the terms of the GNU General Public License as published by "
           "the Free Software Foundation, either version 3 of the License, or "
           "(at your option) any later version.\n\n"
           "Viewnior is distributed in the hope that it will be useful, "
           "but WITHOUT ANY WARRANTY; without even the implied warranty of "
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
           "GNU General Public License for more details.\n\n"
           "You should have received a copy of the GNU General Public License "
           "along with Viewnior.  If not, see <http://www.gnu.org/licenses/>.\n");

    gtk_show_about_dialog (GTK_WINDOW (window),
                   "program-name", "Viewnior",
                   "version", VERSION,
                   "copyright", "Copyright \xc2\xa9 2009-2018 Siyan Panayotov <contact@siyanpanayotov.com>",
                   "comments",_("Elegant Image Viewer"),
                   "authors", authors,
                   "logo-icon-name", "viewnior",
                   "wrap-license", TRUE,
                   "license", license,
                   "website", "http://siyanpanayotov.com/project/viewnior/",
                   "translator-credits", _( "translator-credits" ),
                   NULL);
}

static void
vnr_set_wallpaper(GtkAction *action, VnrWindow *win)
{
    pid_t pid;
    int screen_number = 0;

#ifdef GDK_WINDOWING_X11
    GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (win));
    if (GDK_IS_X11_SCREEN (screen))
        screen_number = gdk_x11_screen_get_screen_number (screen);
#endif

    pid = fork();

    if ( pid == 0 ) {
        gchar * tmp;

        VnrPrefsDesktop desktop_environment = win->prefs->desktop;

        if (desktop_environment == VNR_PREFS_DESKTOP_AUTO)
        {
            desktop_environment = uni_detect_desktop_environment();
        }

        switch(desktop_environment) {
            case VNR_PREFS_DESKTOP_GNOME2:
                execlp("gconftool-2", "gconftool-2",
                        "--set", "/desktop/gnome/background/picture_filename",
                        "--type", "string",
                        VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_MATE:
                execlp("gsettings", "gsettings",
                        "set", "org.mate.background",
                        "picture-filename", VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_GNOME3:
                tmp = g_strdup_printf("file://%s", VNR_FILE(win->file_list->data)->path);
                execlp("gsettings", "gsettings",
                        "set", "org.gnome.desktop.background",
                        "picture-uri", tmp,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_XFCE:
                tmp = g_strdup_printf("/backdrop/screen%d/monitor0/workspace0/last-image",
                                        screen_number);
                execlp("xfconf-query", "xfconf-query",
                        "-c", "xfce4-desktop",
                        "-p", tmp,
                        "--type", "string",
                        "--set",
                        VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_LXDE:
                execlp("pcmanfm", "pcmanfm",
                        "--set-wallpaper",
                        VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_PUPPY:
                execlp("set_bg", "set_bg",
                        VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_FLUXBOX:
                execlp("fbsetbg", "fbsetbg",
                        "-f", VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_NITROGEN:
                execlp("nitrogen", "nitrogen",
                        "--set-zoom-fill", "--save",
                        VNR_FILE(win->file_list->data)->path,
                        NULL);
                break;
            case VNR_PREFS_DESKTOP_CINNAMON:
                tmp = g_strdup_printf("file://%s", VNR_FILE(win->file_list->data)->path);
                execlp("gsettings", "gsettings",
                        "set", "org.cinnamon.desktop.background",
                        "picture-uri", tmp,
                        NULL);
                break;
            default:
                _exit(0);
        }
    } else {
        wait(NULL);
    }
}

enum {
    VNR_COPY_TARGET_URI,
    VNR_COPY_TARGET_GNOME_FILES,
    VNR_COPY_TARGET_IMAGE,
    VNR_COPY_TARGET_TEXT
};

typedef struct {
    GdkPixbuf *pixbuf;
    gchar *path;
    gchar *uri;
} VnrCopyData;

static void
vnr_copy_data_free(gpointer user_data)
{
    VnrCopyData *data = user_data;

    g_object_unref(data->pixbuf);
    g_free(data->path);
    g_free(data->uri);
    g_free(data);
}

static void
vnr_copy_clear_cb(GtkClipboard *clipboard, gpointer user_data)
{
    vnr_copy_data_free(user_data);
}

static void
vnr_copy_get_cb(GtkClipboard *clipboard,
                GtkSelectionData *selection_data,
                guint info,
                gpointer user_data)
{
    VnrCopyData *data = user_data;

    switch (info) {
        case VNR_COPY_TARGET_URI: {
            gchar *uris[] = { data->uri, NULL };
            gtk_selection_data_set_uris(selection_data, uris);
            break;
        }
        case VNR_COPY_TARGET_GNOME_FILES: {
            gchar *buf = g_strconcat("copy\n", data->uri, NULL);
            gtk_selection_data_set(selection_data,
                                   gtk_selection_data_get_target(selection_data),
                                   8, (const guchar *)buf, strlen(buf));
            g_free(buf);
            break;
        }
        case VNR_COPY_TARGET_IMAGE:
            gtk_selection_data_set_pixbuf(selection_data, data->pixbuf);
            break;
        case VNR_COPY_TARGET_TEXT:
            gtk_selection_data_set_text(selection_data, data->path, -1);
            break;
    }
}

/* Puts the image on the clipboard together with its file: apps that accept
 * file pastes get the original file (and its name), text fields such as
 * terminals get the path, and image-only consumers get the pixels. */
static void
vnr_copy_image(GtkAction *action, VnrWindow *win)
{
    GdkPixbuf *view_pixbuf = uni_image_view_get_pixbuf(UNI_IMAGE_VIEW(win->view));

    if (view_pixbuf) {
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        GtkTargetList *list = gtk_target_list_new(NULL, 0);
        GtkTargetEntry *targets;
        gint n_targets;
        VnrCopyData *data = g_new0(VnrCopyData, 1);

        data->pixbuf = g_object_ref(view_pixbuf);
        /* Unsaved rotations, flips and crops exist only in the view, so
         * the file on disk no longer matches: copy just the pixels. */
        if (win->file_list != NULL && win->modifications == 0) {
            const gchar *path = VNR_FILE(win->file_list->data)->path;
            data->uri = g_filename_to_uri(path, NULL, NULL);
            /* Text targets must be UTF-8, but the path is in the
             * filesystem encoding; skip them if it can't be converted. */
            if (data->uri != NULL)
                data->path = g_filename_to_utf8(path, -1, NULL, NULL, NULL);
        }

        if (data->uri != NULL) {
            gtk_target_list_add_uri_targets(list, VNR_COPY_TARGET_URI);
            gtk_target_list_add(list,
                                gdk_atom_intern_static_string("x-special/gnome-copied-files"),
                                0, VNR_COPY_TARGET_GNOME_FILES);
        }
        gtk_target_list_add_image_targets(list, VNR_COPY_TARGET_IMAGE, TRUE);
        if (data->path != NULL)
            gtk_target_list_add_text_targets(list, VNR_COPY_TARGET_TEXT);

        targets = gtk_target_table_new_from_list(list, &n_targets);

        if (gtk_clipboard_set_with_data(clipboard, targets, n_targets,
                                        vnr_copy_get_cb, vnr_copy_clear_cb,
                                        data)) {
            gtk_clipboard_set_can_store(clipboard, targets, n_targets);
            gtk_clipboard_store(clipboard);
        } else {
            vnr_copy_data_free(data);
        }

        gtk_target_table_free(targets, n_targets);
        gtk_target_list_unref(list);

        vnr_message_area_show(VNR_MESSAGE_AREA(win->msg_area),
                              FALSE,
                              _("Image copied to clipboard."),
                              FALSE);

        g_timeout_add_seconds(3, (GSourceFunc)vnr_message_area_hide, VNR_MESSAGE_AREA(win->msg_area));
    }
}

static void
vnr_window_cmd_fullscreen (GtkAction *action, VnrWindow *window)
{
    gboolean fullscreen = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));

    if (fullscreen)
        vnr_window_fullscreen (window);
    else
        vnr_window_unfullscreen (window);
}

static void
vnr_window_cmd_menu_bar (GtkAction *action, VnrWindow *window)
{
    gboolean show = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
    vnr_prefs_set_show_menu_bar(window->prefs, show);

    if (window->mode != VNR_WINDOW_MODE_NORMAL)
       return;


    if (show)
    {
        gtk_widget_show (window->menu_bar);
        gtk_widget_hide (window->properties_button);
    }
    else
    {
        gtk_widget_hide (window->menu_bar);
        gtk_widget_show (window->properties_button);
    }
}

static void
vnr_window_cmd_toolbar (GtkAction *action, VnrWindow *window)
{
    gboolean show = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
    vnr_prefs_set_show_toolbar(window->prefs, show);

    if (show)
        gtk_widget_show (window->toolbar);
    else
        gtk_widget_hide (window->toolbar);
}

static void
vnr_window_cmd_scrollbar (GtkAction *action, VnrWindow *window)
{
    gboolean show = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
    vnr_prefs_set_show_scrollbar (window->prefs, show);
    uni_scroll_win_set_show_scrollbar (UNI_SCROLL_WIN (window->scroll_view), show);
}

static void
vnr_window_cmd_statusbar (GtkAction *action, VnrWindow *window)
{
    gboolean show = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
    vnr_prefs_set_show_statusbar(window->prefs, show);

    if (show)
        gtk_widget_show (window->statusbar);
    else
        gtk_widget_hide (window->statusbar);
}

static void
vnr_window_cmd_slideshow (GtkAction *action, VnrWindow *window)
{
    g_assert(window != NULL && VNR_IS_WINDOW(window));

    if(!window->slideshow)
        return;

    gboolean slideshow;

    slideshow = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));

    if (slideshow && window->mode != VNR_WINDOW_MODE_SLIDESHOW)
    {
        /* ! Uncomment to force Fullscreen along with Slideshow */
        if(window->mode == VNR_WINDOW_MODE_NORMAL)
        {
            vnr_window_fullscreen (window);
        }
        start_slideshow(window);
    }
    else if(window->mode == VNR_WINDOW_MODE_SLIDESHOW)
    {
        /* ! Uncomment to force Fullscreen along with Slideshow */
        vnr_window_unfullscreen (window);
        stop_slideshow(window);
    }
}

/* Asks the user to confirm removing the file shown, in a dialog with
 * the text @primary and @secondary (may be NULL). Returns TRUE if they
 * chose @button. */
static gboolean
vnr_window_confirm_delete (VnrWindow *window, const gchar *primary,
                           const gchar *secondary, const gchar *button)
{
    GtkWidget *dlg;
    gint response;

    dlg = gtk_message_dialog_new (GTK_WINDOW (window),
                                  GTK_DIALOG_MODAL,
                                  GTK_MESSAGE_WARNING,
                                  GTK_BUTTONS_NONE,
                                  "%s", primary);
    if (secondary != NULL)
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dlg),
                                                  "%s", secondary);

    gtk_dialog_add_buttons (GTK_DIALOG (dlg),
                            VNR_GTK_LABEL ("_Cancel"), GTK_RESPONSE_CANCEL,
                            button, GTK_RESPONSE_YES,
                            NULL);

    response = gtk_dialog_run (GTK_DIALOG (dlg));
    gtk_widget_destroy (dlg);

    return response == GTK_RESPONSE_YES;
}

/* Moves the file shown to the trash, asking first if the preferences say
 * so. Where its file system has no trash, offers to delete it permanently
 * instead, and always asks before that. Returns TRUE if the file is gone.
 * On FALSE, @error is set unless the user said no. */
static gboolean
vnr_window_trash_current (VnrWindow *window, GError **error)
{
    VnrFile *vnr_file = VNR_FILE (window->file_list->data);
    GError *trash_error = NULL;
    GFile *file;
    gchar *prompt;
    gboolean removed;

    if (window->prefs->confirm_delete)
    {
        gboolean confirmed;

        /* I18N: The '%s' is replaced with the name of the file. */
        prompt = g_strdup_printf (_("Move \"%s\" to the trash?"),
                                  vnr_file->display_name);
        confirmed = vnr_window_confirm_delete (window, prompt, NULL,
                                               _("Move to _Trash"));
        g_free (prompt);

        if (!confirmed)
            return FALSE;
    }

    file = g_file_new_for_path (vnr_file->path);
    removed = g_file_trash (file, NULL, &trash_error);

    if (!removed && g_error_matches (trash_error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
    {
        /* I18N: The '%s' is replaced with the name of the file. */
        prompt = g_strdup_printf (_("\"%s\" cannot be moved to the trash. "
                                    "Do you want to delete it permanently?"),
                                  vnr_file->display_name);
        if (vnr_window_confirm_delete (window, prompt,
                                       _("If you delete an item, it will be permanently lost."),
                                       _("_Delete")))
        {
            removed = g_file_delete (file, NULL, error);
        }
        g_free (prompt);
        g_clear_error (&trash_error);
    }
    else if (!removed)
    {
        g_propagate_error (error, trash_error);
    }

    g_object_unref (file);
    return removed;
}

/* Deletes the file shown permanently. Always asks first, whatever the
 * preferences say, as there is no getting the file back. Returns TRUE if
 * the file is gone. On FALSE, @error is set unless the user said no. */
static gboolean
vnr_window_delete_current (VnrWindow *window, GError **error)
{
    VnrFile *vnr_file = VNR_FILE (window->file_list->data);
    GFile *file;
    gchar *prompt;
    gboolean confirmed;
    gboolean removed;

    /* I18N: The '%s' is replaced with the name of the file to be deleted. */
    prompt = g_strdup_printf (_("Are you sure you want to\n"
                                "permanently delete \"%s\"?"),
                              vnr_file->display_name);
    confirmed = vnr_window_confirm_delete (window, prompt,
                                           _("If you delete an item, it will be permanently lost."),
                                           _("_Delete"));
    g_free (prompt);

    if (!confirmed)
        return FALSE;

    file = g_file_new_for_path (vnr_file->path);
    removed = g_file_delete (file, NULL, error);
    g_object_unref (file);

    return removed;
}

/* Removes the file shown, to the trash or @permanently, and shows the
 * next image. */
static void
vnr_window_remove_current (VnrWindow *window, gboolean permanently)
{
    GError *error = NULL;
    gboolean restart_slideshow = FALSE;
    gboolean restart_autohide_timeout = FALSE;
    gboolean cursor_was_hidden = FALSE;

    g_return_if_fail (window->file_list != NULL);

    if(window->mode == VNR_WINDOW_MODE_SLIDESHOW)
    {
       stop_slideshow(window);
       restart_slideshow = TRUE;
    }

    if(window->cursor_is_hidden)
    {
        cursor_was_hidden = TRUE;
        vnr_window_show_cursor(window);
    }
    window->disable_autohide = TRUE;

    if(window->fs_source != NULL)
        restart_autohide_timeout = TRUE;

    if(permanently ? vnr_window_delete_current(window, &error)
                   : vnr_window_trash_current(window, &error))
    {
        GList *next;

        next = g_list_next(window->file_list);
        if(next == NULL)
            next = g_list_first(window->file_list);

        if(g_list_length(g_list_first(window->file_list)) != 1)
            window->file_list = g_list_delete_link (window->file_list, window->file_list);
        else
        {
            g_list_free(window->file_list);
            next = NULL;
        }

        if(next == NULL)
        {
            vnr_window_set_list(window, NULL, FALSE);
            vnr_window_show_load_error(window,
                                       _("The given locations contain no images."));
            restart_slideshow = FALSE;

            if(gtk_widget_get_visible(window->props_dlg))
                vnr_properties_dialog_clear(VNR_PROPERTIES_DIALOG(window->props_dlg));
        }
        else
        {
            vnr_window_set_list(window, next, FALSE);
            vnr_window_close(window);
            vnr_window_open(window, FALSE);
        }
    }
    else if(error != NULL)
    {
        vnr_message_area_show(VNR_MESSAGE_AREA (window->msg_area), TRUE,
                              error->message, FALSE);
        g_error_free(error);
        restart_slideshow = FALSE;
    }

    window->disable_autohide = FALSE;

    if(restart_slideshow)
       start_slideshow(window);
    if(cursor_was_hidden)
        vnr_window_hide_cursor(window);
    if(restart_autohide_timeout)
        fullscreen_set_timeout(window);
}

static void
vnr_window_cmd_trash (GtkAction *action, VnrWindow *window)
{
    vnr_window_remove_current (window, FALSE);
}

static void
vnr_window_cmd_delete (GtkAction *action, VnrWindow *window)
{
    vnr_window_remove_current (window, TRUE);
}

static void
vnr_window_cmd_crop(GtkAction *action, VnrWindow *window)
{
    VnrCrop *crop;

    if ( !gtk_action_group_get_sensitive(window->actions_static_image) )
        return;

    crop = (VnrCrop*) vnr_crop_new (window);

    if(! vnr_crop_run(crop))
    {
        g_object_unref(crop);
        return;
    }

    GdkPixbuf *cropped;
    GdkPixbuf *original;

    original = uni_image_view_get_pixbuf(UNI_IMAGE_VIEW(window->view));

    cropped = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (original),
                               gdk_pixbuf_get_has_alpha (original),
                               gdk_pixbuf_get_bits_per_sample (original),
                               crop->area.width, crop->area.height);

    gdk_pixbuf_copy_area((const GdkPixbuf*)original, crop->area.x, crop->area.y,
                         crop->area.width, crop->area.height, cropped, 0, 0);

    g_object_unref(crop);

    if(!vnr_window_show_edited(window, cropped))
        return;

    window->modifications |= 8;
    vnr_window_image_modified(window);
}

static const GtkActionEntry action_entries_window[] = {
    { "File",  NULL, N_("_File") },
    { "Edit",  NULL, N_("_Edit") },
    { "View",  NULL, N_("_View") },
    { "Image",  NULL, N_("_Image") },
    { "Go",    NULL, N_("_Go") },
    { "Help",  NULL, N_("_Help") },

    { "FileOpen", GTK_STOCK_FILE, N_("Open _Image..."), "<control>O",
      N_("Open an Image"),
      G_CALLBACK (vnr_window_cmd_open) },
    { "FileOpenDir", GTK_STOCK_DIRECTORY, N_("Open _Folder..."), "<control>F",
      N_("Open a Folder"),
      G_CALLBACK (vnr_window_cmd_open_dir) },
    { "FileClose", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
      N_("Close window"),
      G_CALLBACK (gtk_main_quit) },
    { "HelpAbout", GTK_STOCK_ABOUT, N_("_About"), NULL,
      N_("About this application"),
      G_CALLBACK (vnr_window_cmd_about) },
    { "EditPreferences", GTK_STOCK_PREFERENCES, N_("_Preferences..."), NULL,
      N_("User preferences for Viewnior"),
      G_CALLBACK (vnr_window_cmd_preferences) }
};

static const GtkActionEntry action_entry_save[] = {
    { "FileSave", GTK_STOCK_SAVE, N_("_Save"), "<control>S",
      N_("Save changes"),
      G_CALLBACK (save_image_cb) },
};

static const GtkToggleActionEntry toggle_entry_properties[] = {
    { "Properties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
      N_("Properties"),
      G_CALLBACK (vnr_window_cmd_open_menu) },
};

static const GtkActionEntry action_entry_wallpaper[] = {
    { "SetAsWallpaper", NULL, N_("Set as _Wallpaper"), "<control>F8",
      N_("Set the selected image as the desktop background"),
      G_CALLBACK (vnr_set_wallpaper) },
};

static const GtkActionEntry action_entry_copy_image[] = {
    { "CopyImage", NULL, N_("Copy image to clipboard"), "<control>C",
      N_("Copy the image to the clipboard"),
      G_CALLBACK (vnr_copy_image) },
};

static const GtkActionEntry action_entries_image[] = {
    { "FileOpenWith", NULL, N_("Open _With"), NULL,
      N_("Open the selected image with a different application"),
      NULL},
    { "FileDelete", "user-trash", N_("Move to _Trash"), NULL,
      N_("Move the current file to the trash"),
      G_CALLBACK (vnr_window_cmd_trash) },
    { "FilePermanentDelete", "edit-delete", N_("_Delete"), NULL,
      N_("Permanently delete the current file"),
      G_CALLBACK (vnr_window_cmd_delete) },
    { "FileProperties", GTK_STOCK_PROPERTIES, N_("_Properties..."), "<Alt>Return",
      N_("Show information about the current file"),
      G_CALLBACK (vnr_window_cmd_properties) },
    { "FileReload", GTK_STOCK_REFRESH, N_("_Reload"), NULL,
      N_("Reload the current file"),
      G_CALLBACK (vnr_window_cmd_reload) },
    { "Delete", NULL, N_("Move to _Trash"), "Delete",
      N_("Move the current file to the trash"),
      G_CALLBACK (vnr_window_cmd_trash) },
    { "PermanentDelete", NULL, N_("_Delete"), "<shift>Delete",
      N_("Permanently delete the current file"),
      G_CALLBACK (vnr_window_cmd_delete) },
    { "ViewZoomIn", GTK_STOCK_ZOOM_IN, N_("_Zoom In"), "<control>plus",
      N_("Enlarge the image"),
      G_CALLBACK (vnr_window_cmd_zoom_in) },
    { "ViewZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"), "<control>minus",
      N_("Shrink the image"),
      G_CALLBACK (vnr_window_cmd_zoom_out) },
    { "ViewZoomNormal", GTK_STOCK_ZOOM_100, N_("_Normal Size"), "<control>0",
      N_("Show the image at its normal size"),
      G_CALLBACK (vnr_window_cmd_normal_size) },
    { "ViewZoomFit", GTK_STOCK_ZOOM_FIT, N_("Best _Fit"), NULL,
      N_("Fit the image to the window"),
      G_CALLBACK (vnr_window_cmd_fit) },
    { "ViewZoom50", NULL, N_("Zoom _50%"), NULL,
      N_("Show the image at half its normal size"),
      G_CALLBACK (vnr_window_cmd_zoom_50) },
    { "ControlEqual", GTK_STOCK_ZOOM_IN, N_("_Zoom In"), "<control>equal",
      N_("Shrink the image"),
      G_CALLBACK (vnr_window_cmd_zoom_in) },
    { "ControlKpAdd", GTK_STOCK_ZOOM_IN, N_("_Zoom In"), "<control>KP_Add",
      N_("Shrink the image"),
      G_CALLBACK (vnr_window_cmd_zoom_in) },
    { "ControlKpSub", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"), "<control>KP_Subtract",
      N_("Shrink the image"),
      G_CALLBACK (vnr_window_cmd_zoom_out) },
};

static const GtkActionEntry action_entries_static_image[] = {
    { "ImageRotateCW", "object-rotate-right", N_("Rotate _Clockwise"), "<control>R",
      N_("Rotate image clockwise"),
      G_CALLBACK (vnr_window_cmd_rotate_cw) },
    { "ImageRotateCCW", "object-rotate-left", N_("Rotate _Anti-clockwise"), "<control><shift>R",
      N_("Rotate image anti-clockwise"),
      G_CALLBACK (vnr_window_cmd_rotate_ccw) },
    { "ImageFlipVertical", "object-flip-vertical", N_("Flip _Vertical"), NULL,
      N_("Flip image vertically"),
      G_CALLBACK (vnr_window_cmd_flip_vertical) },
    { "ImageFlipHorizontal", "object-flip-horizontal", N_("Flip _Horizontal"), NULL,
      N_("Flip image horizontally"),
      G_CALLBACK (vnr_window_cmd_flip_horizontal) },
    { "ImageCrop", NULL, N_("Crop..."), NULL,
      N_("Crop"),
      G_CALLBACK (vnr_window_cmd_crop) },
};

static const GtkToggleActionEntry toggle_entries_image[] = {
    { "ViewFullscreen", GTK_STOCK_FULLSCREEN, N_("Full _Screen"), "F11",
      N_("Show in fullscreen mode"),
      G_CALLBACK (vnr_window_cmd_fullscreen) },
    { "ViewResizeWindow", NULL, N_("_Adjust window size"), NULL,
      N_("Adjust window size to fit the image"),
      G_CALLBACK (vnr_window_cmd_resize) },
};

static const GtkToggleActionEntry toggle_entries_window[] = {
    { "ViewMenuBar", NULL, N_("Menu Bar"), NULL,
      N_("Show Menu Bar"),
      G_CALLBACK (vnr_window_cmd_menu_bar) },
    { "ViewToolbar", NULL, N_("Toolbar"), NULL,
      N_("Show Toolbar"),
      G_CALLBACK (vnr_window_cmd_toolbar) },
    { "ViewScrollbar", NULL, N_("Scrollbar"), NULL,
      N_("Show Scrollbar"),
      G_CALLBACK (vnr_window_cmd_scrollbar) },
    { "ViewStatusbar", NULL, N_("Statusbar"), NULL,
      N_("Show Statusbar"),
      G_CALLBACK (vnr_window_cmd_statusbar) },
};

static const GtkToggleActionEntry toggle_entries_collection[] = {
    { "ViewSlideshow", GTK_STOCK_NETWORK, N_("Sli_deshow"), "F5",
      N_("Show in slideshow mode"),
      G_CALLBACK (vnr_window_cmd_slideshow) },
};

static const GtkActionEntry action_entries_collection[] = {
    { "GoPrevious", GTK_STOCK_GO_BACK, N_("_Previous Image"), "<Alt>Left",
      N_("Go to the previous image of the collection"),
      G_CALLBACK (vnr_window_cmd_prev) },
    { "GoNext", GTK_STOCK_GO_FORWARD, N_("_Next Image"), "<Alt>Right",
      N_("Go to the next image of the collection"),
      G_CALLBACK (vnr_window_cmd_next) },
    { "GoFirst", GTK_STOCK_GOTO_FIRST, N_("_First Image"), "<Alt>Home",
      N_("Go to the first image of the collection"),
      G_CALLBACK (vnr_window_cmd_first) },
    { "GoLast", GTK_STOCK_GOTO_LAST, N_("_Last Image"), "<Alt>End",
      N_("Go to the last image of the collection"),
      G_CALLBACK (vnr_window_cmd_last) },
};

/*************************************************************/
/***** Stuff that deals with the type ************************/
/*************************************************************/
/* Modified version of eog's eog_window_key_press */
static gint
vnr_window_key_press (GtkWidget *widget, GdkEventKey *event)
{
    gint result = FALSE;
    VnrWindow *window = VNR_WINDOW(widget);
    GtkWidget *toolbar_focus_child;
    GtkWidget *msg_area_focus_child;

    toolbar_focus_child = gtk_container_get_focus_child(GTK_CONTAINER(window->toolbar));
    msg_area_focus_child = gtk_container_get_focus_child(GTK_CONTAINER(window->msg_area));

    switch(event->keyval){
        case GDK_KEY_Left:
            if (event->state & GDK_MOD1_MASK)
            {
                vnr_window_cmd_prev (NULL, window);
                result = TRUE;
                break;
            } /* else fall-trough is intended */
        case GDK_KEY_Up:
            if (!uni_scroll_win_image_fits (UNI_SCROLL_WIN (window->scroll_view)))
            {
                /* break to let scrollview handle the key */
                break;
            }
            if (toolbar_focus_child != NULL || msg_area_focus_child != NULL)
                break;

            vnr_window_cmd_prev (NULL, window);
            result = TRUE;
            break;
        case GDK_KEY_Right:
            if (event->state & GDK_MOD1_MASK)
            {
                vnr_window_cmd_next (NULL, window);
                result = TRUE;
                break;
            } /* else fall-trough is intended */
        case GDK_KEY_Down:
            if (!uni_scroll_win_image_fits (UNI_SCROLL_WIN (window->scroll_view)))
            {
                /* break to let scrollview handle the key */
                break;
            }
            if (toolbar_focus_child != NULL || msg_area_focus_child != NULL)
                break;

            vnr_window_cmd_next (NULL, window);
            result = TRUE;
            break;
        case GDK_KEY_Page_Up:
            vnr_window_cmd_prev (NULL, window);
            result = TRUE;
            break;
        case GDK_KEY_Page_Down:
            vnr_window_cmd_next (NULL, window);
            result = TRUE;
            break;
        case GDK_KEY_Escape:
        case 'q':
            if(window->mode != VNR_WINDOW_MODE_NORMAL)
                vnr_window_unfullscreen(window);
            else
                gtk_main_quit();
            break;
        case GDK_KEY_space:
            if (toolbar_focus_child != NULL || msg_area_focus_child != NULL)
                break;
            vnr_window_next(window, TRUE);
            result = TRUE;
            break;
        case GDK_KEY_BackSpace:
            vnr_window_prev(window);
            result = TRUE;
            break;
        case GDK_KEY_Home:
            vnr_window_first(window);
            result = TRUE;
            break;
        case GDK_KEY_End:
            vnr_window_last(window);
            result = TRUE;
            break;
        case GDK_KEY_h:
            vnr_window_cmd_flip_horizontal(NULL, window);
            break;
        case GDK_KEY_v:
            vnr_window_cmd_flip_vertical(NULL, window);
            break;
        case GDK_KEY_c:
            // ctrl + c is used to copy, the bind is added in action_entry_copy_image
            if (!(event->state & GDK_CONTROL_MASK)) {
                vnr_window_cmd_crop(NULL, window);
            }
            break;
        case GDK_KEY_r:
            // ctrl + r is rotate, the bind is added in action_entries_static_image
            if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))
                break;
            if (toolbar_focus_child != NULL || msg_area_focus_child != NULL)
                break;
            result = vnr_window_random(window);
            break;
    }

    if (result == FALSE && GTK_WIDGET_CLASS (vnr_window_parent_class)->key_press_event)
        result = (* GTK_WIDGET_CLASS (vnr_window_parent_class)->key_press_event) (widget, event);

    return result;
}

static void
vnr_window_drag_data_received (GtkWidget *widget,
                               GdkDragContext *context,
                               gint x, gint y,
                               GtkSelectionData *selection_data,
                               guint info, guint time)
{
    GSList *uri_list = NULL;
    GdkAtom target = gtk_selection_data_get_target (selection_data);
    GdkDragAction suggested_action;

    if (!gtk_targets_include_uri (&target, 1))
        return;

    if (gtk_drag_get_source_widget (context))
        return;

    suggested_action = gdk_drag_context_get_suggested_action (context);
    if (suggested_action == GDK_ACTION_COPY || suggested_action == GDK_ACTION_ASK)
    {
        const guchar *data = gtk_selection_data_get_data (selection_data);

        uri_list = vnr_tools_parse_uri_string_list_to_file_list ((gchar *) data);
        if(uri_list == NULL)
        {
            vnr_window_show_load_error(VNR_WINDOW (widget),
                                       _("The given locations contain no images."));
            return;
        }

        vnr_window_open_from_list(VNR_WINDOW (widget), uri_list);
    }
}

static void
vnr_window_class_init (VnrWindowClass * klass)
{
    GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

    widget_class->key_press_event = vnr_window_key_press;
    widget_class->drag_data_received = vnr_window_drag_data_received;
}

GtkWindow *
vnr_window_new ()
{
    return (GtkWindow *) g_object_new (VNR_TYPE_WINDOW, NULL);
}

/* Create an action group with the given actions and toggle actions, and
 * add it to the UI manager. Groups added later take precedence. */
static GtkActionGroup *
vnr_window_add_action_group (VnrWindow *window, const gchar *name,
                             const GtkActionEntry *entries, guint n_entries,
                             const GtkToggleActionEntry *toggle_entries,
                             guint n_toggle_entries)
{
    GtkActionGroup *group = gtk_action_group_new (name);

    gtk_action_group_set_translation_domain (group, GETTEXT_PACKAGE);
    if (entries != NULL)
        gtk_action_group_add_actions (group, entries, n_entries, window);
    if (toggle_entries != NULL)
        gtk_action_group_add_toggle_actions (group, toggle_entries,
                                             n_toggle_entries, window);

    gtk_ui_manager_insert_action_group (window->ui_mngr, group, 0);
    return group;
}

static void
vnr_window_add_ui (VnrWindow *window, const gchar *ui)
{
    GError *error = NULL;

    if (!gtk_ui_manager_add_ui_from_string (window->ui_mngr, ui, -1, &error))
        g_error ("building menus failed: %s\n", error->message);
}

static void
vnr_window_init (VnrWindow * window)
{
    GtkAction *action;

    window->writable_format_name = NULL;
    window->file_list = NULL;
    window->fs_controls = NULL;
    window->fs_source = NULL;
    window->ss_timeout = 5;
    window->slideshow = TRUE;
    window->cursor_is_hidden = FALSE;
    window->disable_autohide = FALSE;
    window->actions_open_with = NULL;
    window->open_with_menu_id = 0;

    window->prefs = (VnrPrefs*)vnr_prefs_new (GTK_WIDGET(window));

    window->mode = VNR_WINDOW_MODE_NORMAL;

    gtk_window_set_title ((GtkWindow *) window, "Viewnior");
    gtk_window_set_default_icon_name ("viewnior");

    /* Build MENUBAR and TOOLBAR */
    window->ui_mngr = gtk_ui_manager_new();

    window->actions_window =
        vnr_window_add_action_group (window, "MenuActionsWindow",
                                     action_entries_window,
                                     G_N_ELEMENTS (action_entries_window),
                                     NULL, 0);
    window->action_save =
        vnr_window_add_action_group (window, "MenuActionSave",
                                     action_entry_save,
                                     G_N_ELEMENTS (action_entry_save),
                                     NULL, 0);
    window->action_properties =
        vnr_window_add_action_group (window, "MenuActionProperties",
                                     NULL, 0,
                                     toggle_entry_properties,
                                     G_N_ELEMENTS (toggle_entry_properties));
    window->actions_static_image =
        vnr_window_add_action_group (window, "MenuActionsStaticImage",
                                     action_entries_static_image,
                                     G_N_ELEMENTS (action_entries_static_image),
                                     NULL, 0);
    window->actions_image =
        vnr_window_add_action_group (window, "MenuActionsImage",
                                     action_entries_image,
                                     G_N_ELEMENTS (action_entries_image),
                                     toggle_entries_image,
                                     G_N_ELEMENTS (toggle_entries_image));
    window->actions_bars =
        vnr_window_add_action_group (window, "MenuActionsBars",
                                     NULL, 0,
                                     toggle_entries_window,
                                     G_N_ELEMENTS (toggle_entries_window));
    window->actions_collection =
        vnr_window_add_action_group (window, "MenuActionsCollection",
                                     action_entries_collection,
                                     G_N_ELEMENTS (action_entries_collection),
                                     toggle_entries_collection,
                                     G_N_ELEMENTS (toggle_entries_collection));
    vnr_window_add_ui (window, ui_definition);

    window->action_wallpaper =
        vnr_window_add_action_group (window, "ActionWallpaper",
                                     action_entry_wallpaper,
                                     G_N_ELEMENTS (action_entry_wallpaper),
                                     NULL, 0);
    vnr_window_add_ui (window, ui_definition_wallpaper);
    gtk_action_group_set_sensitive(window->action_wallpaper, FALSE);

    window->action_copy =
        vnr_window_add_action_group (window, "ActionCopy",
                                     action_entry_copy_image,
                                     G_N_ELEMENTS (action_entry_copy_image),
                                     NULL, 0);
    vnr_window_add_ui (window, ui_definition_copy);
    gtk_action_group_set_sensitive(window->action_copy, FALSE);

    gtk_action_group_set_sensitive(window->actions_collection, FALSE);
    gtk_action_group_set_sensitive(window->actions_image, FALSE);
    gtk_action_group_set_sensitive(window->actions_static_image, FALSE);
    gtk_action_group_set_sensitive(window->action_save, FALSE);
    gtk_action_group_set_sensitive(window->actions_bars, TRUE);

    /* Continue with layout */

    window->layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (window), window->layout);
    gtk_widget_show(window->layout);

    window->menus = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (window->layout), window->menus, FALSE,FALSE,0);

    window->menu_bar = gtk_ui_manager_get_widget (window->ui_mngr, "/MainMenu");
    g_assert(GTK_IS_WIDGET(window->menu_bar));
    gtk_box_pack_start (GTK_BOX (window->menus), window->menu_bar, FALSE,FALSE,0);

    window->properties_button = gtk_ui_manager_get_widget (window->ui_mngr, "/Toolbar/Properties");
    g_assert(GTK_IS_WIDGET(window->properties_button));

    window->button_menu = gtk_ui_manager_get_widget (window->ui_mngr, "/ButtonMenu");
    g_assert(GTK_IS_WIDGET(window->button_menu));
    gtk_menu_attach_to_widget (GTK_MENU(window->button_menu), GTK_WIDGET(window->properties_button), NULL);

    window->toolbar = gtk_ui_manager_get_widget (window->ui_mngr, "/Toolbar");
    g_assert(GTK_IS_WIDGET(window->toolbar));
    gtk_toolbar_set_style(GTK_TOOLBAR(window->toolbar), GTK_TOOLBAR_ICONS);
    g_object_set(G_OBJECT(window->toolbar), "show-arrow", FALSE, NULL);
    gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar),
                        GTK_TOOL_ITEM(get_fs_controls(window)), -1);
    GtkStyleContext *context = gtk_widget_get_style_context(window->toolbar);
    gtk_style_context_add_class(context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
    gtk_box_pack_start (GTK_BOX (window->menus), window->toolbar, FALSE,FALSE,0);

    window->popup_menu = gtk_ui_manager_get_widget (window->ui_mngr, "/PopupMenu");
    g_assert(GTK_IS_WIDGET(window->popup_menu));

    gtk_ui_manager_ensure_update (window->ui_mngr);
    gtk_widget_show_all(window->menus);

    gtk_widget_hide(get_fs_controls(window));

    // Apply menu bar preference
    action = gtk_action_group_get_action (window->actions_bars,
                                          "ViewMenuBar");
    if(!window->prefs->show_menu_bar)
        gtk_widget_hide (window->menu_bar);
    else
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);


    // Apply toolbar preference
    action = gtk_action_group_get_action (window->actions_bars,
                                          "ViewToolbar");
    if(!window->prefs->show_toolbar)
        gtk_widget_hide (window->toolbar);
    else
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

    // Apply auto-resize preference
    action = gtk_action_group_get_action (window->actions_image,
                                          "ViewResizeWindow");

    if(window->prefs->auto_resize)
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

    window->msg_area = vnr_message_area_new();
    VNR_MESSAGE_AREA(window->msg_area)->vnr_win = window;
    gtk_box_pack_start (GTK_BOX (window->layout), window->msg_area, FALSE,FALSE,0);
    gtk_widget_show(GTK_WIDGET (window->msg_area));

    window->view = uni_anim_view_new ();
    gtk_widget_set_can_focus(window->view, TRUE);
    window->scroll_view = uni_scroll_win_new (UNI_IMAGE_VIEW (window->view));


    window->statusbar = gtk_statusbar_new();
    gtk_box_pack_end (GTK_BOX (window->layout), window->statusbar, FALSE,FALSE,0);

    // Apply statusbar preference
    action = gtk_action_group_get_action (window->actions_bars,
                                          "ViewStatusbar");
    if(!window->prefs->show_statusbar)
        gtk_widget_hide (window->statusbar);
    else
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);


    // Apply scrollbar preference
    action = gtk_action_group_get_action (window->actions_bars,
                                          "ViewScrollbar");
    uni_scroll_win_set_show_scrollbar (UNI_SCROLL_WIN (window->scroll_view), window->prefs->show_scrollbar);
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), window->prefs->show_scrollbar);

    gtk_box_pack_end (GTK_BOX (window->layout), window->scroll_view, TRUE,TRUE,0);
    gtk_widget_show_all(GTK_WIDGET (window->scroll_view));

    gtk_widget_grab_focus(window->view);

    // Initialize slideshow timeout
    window->ss_timeout = window->prefs->slideshow_timeout;

    /* Care for Properties dialog */
    window->props_dlg = vnr_properties_dialog_new(window,
                             gtk_action_group_get_action (window->actions_collection,
                                                          "GoNext"),
                             gtk_action_group_get_action (window->actions_collection,
                                                          "GoPrevious"));

    vnr_window_apply_preferences(window);

    vnr_window_set_drag(window);

    g_signal_connect (G_OBJECT (window), "destroy",
                      G_CALLBACK (window_destroy_cb), NULL);

    g_signal_connect (G_OBJECT (window), "realize",
                      G_CALLBACK (window_realize_cb), NULL);

    g_signal_connect (G_OBJECT (window), "window-state-event",
                      G_CALLBACK (window_change_state_cb), NULL);

    g_signal_connect (G_OBJECT (window->view), "zoom_changed",
                      G_CALLBACK (zoom_changed_cb), window);

    g_signal_connect (G_OBJECT (window->view), "drag-data-get",
                      G_CALLBACK (window_drag_begin_cb), window);

    g_signal_connect (G_OBJECT (window->button_menu), "hide",
                      G_CALLBACK(vnr_window_cmd_main_menu_hidden), window);

    gtk_window_add_accel_group (GTK_WINDOW (window),
                gtk_ui_manager_get_accel_group (window->ui_mngr));

     vnr_window_load_accel_map();
}

/*************************************************************/
/***** Actions ***********************************************/
/*************************************************************/
gboolean
vnr_window_open (VnrWindow * window, gboolean fit_to_screen)
{
    VnrFile *file;
    GdkPixbufAnimation *pixbuf;
    GdkPixbufFormat *format;
    UniFittingMode last_fit_mode;
    GError *error = NULL;

    if(window->file_list == NULL)
        return FALSE;

    file = VNR_FILE(window->file_list->data);

    update_fs_filename_label(window);

    pixbuf = gdk_pixbuf_animation_new_from_file (file->path, &error);

    if (error != NULL)
    {
        vnr_message_area_show(VNR_MESSAGE_AREA (window->msg_area),
                              TRUE, error->message, TRUE);

        /* file_list already points at the failed file while the view still
         * shows the previous image; copying would mix the two up, and saving
         * an edit would write the previous image over the failed file. */
        gtk_action_group_set_sensitive(window->action_copy, FALSE);
        gtk_action_group_set_sensitive(window->actions_static_image, FALSE);
        gtk_action_group_set_sensitive(window->action_save, FALSE);
        window->modifications = 0;
        window->orientation_applied = FALSE;
        g_clear_pointer(&window->writable_format_name, g_free);

        if(gtk_widget_get_visible(window->props_dlg))
            vnr_properties_dialog_clear(VNR_PROPERTIES_DIALOG(window->props_dlg));
        return FALSE;
    }

    if(vnr_message_area_is_visible(VNR_MESSAGE_AREA(window->msg_area)))
    {
        vnr_message_area_hide(VNR_MESSAGE_AREA(window->msg_area));
    }

    gtk_action_group_set_sensitive(window->actions_image, TRUE);
    gtk_action_group_set_sensitive(window->action_wallpaper, TRUE);
    gtk_action_group_set_sensitive(window->action_copy, TRUE);

    format = gdk_pixbuf_get_file_info (file->path, NULL, NULL);

    g_free(window->writable_format_name);
    if(gdk_pixbuf_format_is_writable (format))
        window->writable_format_name = gdk_pixbuf_format_get_name (format);
    else
        window->writable_format_name = NULL;

    /* libtiff turns a TIFF as its orientation says while decoding it, all
     * the way for some orientations and partly for others, and gdk-pixbuf
     * reports only what is left to do. Either way a TIFF ends up upright. */
    window->orientation_applied = vnr_tools_apply_embedded_orientation (&pixbuf)
                                  || g_strcmp0 (window->writable_format_name, "tiff") == 0;
    window->current_image_width = gdk_pixbuf_animation_get_width (pixbuf);
    window->current_image_height = gdk_pixbuf_animation_get_height (pixbuf);
    window->modifications = 0;

    if(fit_to_screen)
    {
        vnr_window_resize (window);
    }

    last_fit_mode = UNI_IMAGE_VIEW(window->view)->fitting;

    /* Return TRUE if the image is static */
    if ( uni_anim_view_set_anim (UNI_ANIM_VIEW (window->view), pixbuf) )
        gtk_action_group_set_sensitive(window->actions_static_image, TRUE);
    else
        gtk_action_group_set_sensitive(window->actions_static_image, FALSE);

    if(window->mode != VNR_WINDOW_MODE_NORMAL && window->prefs->fit_on_fullscreen)
    {
        uni_image_view_set_zoom_mode (UNI_IMAGE_VIEW(window->view), VNR_PREFS_ZOOM_FIT);
    }
    else if(window->prefs->zoom == VNR_PREFS_ZOOM_LAST_USED )
    {
        uni_image_view_set_fitting (UNI_IMAGE_VIEW(window->view), last_fit_mode);
        zoom_changed_cb(UNI_IMAGE_VIEW(window->view), window);
    }
    else
    {
        uni_image_view_set_zoom_mode (UNI_IMAGE_VIEW(window->view), window->prefs->zoom);
    }

    if ( window->prefs->auto_resize ) {
        vnr_window_cmd_resize(NULL, window);
    }

    if(gtk_widget_get_visible(window->props_dlg))
        vnr_properties_dialog_update(VNR_PROPERTIES_DIALOG(window->props_dlg));

    vnr_window_update_openwith_menu (window);

    g_object_unref(pixbuf);
    return TRUE;
}

gboolean
vnr_window_set_list_from_paths(VnrWindow *window, GSList *uri_list)
{
    GList *file_list = NULL;
    GError *error = NULL;

    /* A single file is loaded along with the other images in its folder,
     * positioned at that file */
    if(g_slist_length(uri_list) == 1)
        vnr_file_load_single_uri (uri_list->data, &file_list, window->prefs->show_hidden, &error);
    else
        vnr_file_load_uri_list (uri_list, &file_list, window->prefs->show_hidden, &error);

    if(error != NULL)
    {
        vnr_window_show_load_error(window, error->message);
        g_error_free(error);

        /* The other images in the folder of a file that isn't one. Keep
         * this after the error, so the user can move to them. */
        if(file_list != NULL)
            vnr_window_set_list(window, file_list, TRUE);
        return FALSE;
    }

    if(file_list == NULL)
    {
        vnr_window_show_load_error(window, _("The given locations contain no images."));
        return FALSE;
    }

    vnr_window_set_list(window, file_list, TRUE);
    return TRUE;
}

void
vnr_window_open_from_list(VnrWindow *window, GSList *uri_list)
{
    if(!vnr_window_set_list_from_paths(window, uri_list))
        return;

    vnr_window_set_busy(window, TRUE);
    vnr_window_close(window);
    vnr_window_open(window, FALSE);
    vnr_window_set_busy(window, FALSE);
}

void
vnr_window_close(VnrWindow *window)
{
    gtk_window_set_title (GTK_WINDOW (window), "Viewnior");
    uni_anim_view_set_anim (UNI_ANIM_VIEW (window->view), NULL);
    gtk_action_group_set_sensitive(window->actions_image, FALSE);
    gtk_action_group_set_sensitive(window->action_wallpaper, FALSE);
    gtk_action_group_set_sensitive(window->action_copy, FALSE);
    gtk_action_group_set_sensitive(window->actions_static_image, FALSE);
}

void
vnr_window_set_list (VnrWindow *window, GList *list, gboolean free_current)
{
    if (free_current == TRUE && window->file_list != NULL)
        g_list_free (window->file_list);
    if (g_list_length(g_list_first(list)) > 1)
    {
        gtk_action_group_set_sensitive(window->actions_collection, TRUE);
        allow_slideshow(window);
    }
    else
    {
        gtk_action_group_set_sensitive(window->actions_collection, FALSE);
        deny_slideshow(window);
    }
    window->file_list = list;
}

/* Show @target, a link of the file list. In a slideshow, the countdown to
 * the next image starts over if @restart_timer is TRUE. */
static void
vnr_window_go_to (VnrWindow *window, GList *target, gboolean restart_timer)
{
    gboolean restart = restart_timer && window->mode == VNR_WINDOW_MODE_SLIDESHOW;

    if(vnr_message_area_is_critical(VNR_MESSAGE_AREA(window->msg_area)))
    {
        vnr_message_area_hide(VNR_MESSAGE_AREA(window->msg_area));
    }

    if(restart)
        g_source_remove (window->ss_source_tag);

    window->file_list = target;

    vnr_window_set_busy(window, TRUE);
    vnr_window_open(window, FALSE);
    vnr_window_set_busy(window, FALSE);

    if(restart)
        slideshow_schedule_next(window);
}

gboolean
vnr_window_next (VnrWindow *window, gboolean rem_timeout){
    GList *next;

    /* Don't reload current image
     * if the list contains only one (or no) image */
    if (g_list_length(g_list_first(window->file_list)) <2)
        return FALSE;

    next = g_list_next(window->file_list);
    if(next == NULL)
    {
        next = g_list_first(window->file_list);
    }

    vnr_window_go_to(window, next, rem_timeout);
    return TRUE;
}

gboolean
vnr_window_prev (VnrWindow *window){
    GList *prev;

    /* Don't reload current image
     * if the list contains only one (or no) image */
    if (g_list_length(g_list_first(window->file_list)) <2)
        return FALSE;

    prev = g_list_previous(window->file_list);
    if(prev == NULL)
    {
        prev = g_list_last(window->file_list);
    }

    vnr_window_go_to(window, prev, TRUE);
    return TRUE;
}

gboolean
vnr_window_first (VnrWindow *window){
    vnr_window_go_to(window, g_list_first(window->file_list), TRUE);
    return TRUE;
}

gboolean
vnr_window_last (VnrWindow *window){
    vnr_window_go_to(window, g_list_last(window->file_list), TRUE);
    return TRUE;
}

/* Jump to a random image of the collection, never the one being shown. */
gboolean
vnr_window_random (VnrWindow *window)
{
    GList *first;
    guint length;
    gint current;
    gint pos;

    first = g_list_first(window->file_list);
    length = g_list_length(first);

    /* There is nothing else to pick with fewer than two images */
    if (length < 2)
        return FALSE;

    /* Pick from the other length - 1 images, skipping over the current one */
    current = g_list_position(first, window->file_list);
    pos = g_random_int_range(0, length - 1);
    if (pos >= current)
        pos++;

    vnr_window_go_to(window, g_list_nth(first, pos), TRUE);
    return TRUE;
}

void
vnr_window_apply_preferences (VnrWindow *window)
{
    if ( window->prefs->dark_background ) {
        vnr_window_set_view_background(window, DARK_BACKGROUND_COLOR);
    }

    if(window->prefs->smooth_images && UNI_IMAGE_VIEW(window->view)->interp != GDK_INTERP_BILINEAR)
    {
        UNI_IMAGE_VIEW(window->view)->interp = GDK_INTERP_BILINEAR;
        gtk_widget_queue_draw(window->view);
    }
    else if(!window->prefs->smooth_images && UNI_IMAGE_VIEW(window->view)->interp != GDK_INTERP_NEAREST)
    {
        UNI_IMAGE_VIEW(window->view)->interp = GDK_INTERP_NEAREST;
        gtk_widget_queue_draw(window->view);
    }


    if(gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(window->ss_timeout_widget)) != window->prefs->slideshow_timeout)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(window->ss_timeout_widget), (gdouble) window->prefs->slideshow_timeout);
    }
}

void
vnr_window_toggle_fullscreen (VnrWindow *window)
{
    gboolean fullscreen;

    fullscreen = (window->mode == VNR_WINDOW_MODE_NORMAL)?TRUE:FALSE;

    if (fullscreen)
        vnr_window_fullscreen (window);
    else
        vnr_window_unfullscreen (window);
}
