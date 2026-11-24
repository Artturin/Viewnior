/*
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

/* Tests of the main window. They need a display; meson runs them under
 * xvfb-run, and skips them when it isn't installed. */

#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include "vnr-window.h"
#include "vnr-file.h"
#include "vnr-message-area.h"
#include "uni-image-view.h"
#include "uni-zoom.h"
#include "test-helpers.h"

#define TIMEOUT_MS 5000

#define SMALL_W 64
#define SMALL_H 48
#define BIG_W 2000
#define BIG_H 1500

/* Folders shared by all tests */
static gchar *images_dir;   /* a.png b.png big.png c.png */
static gchar *mixed_dir;    /* d.png e.png notes.txt */
static gchar *single_dir;   /* only.png */
static gchar *empty_dir;

typedef struct {
    VnrWindow *window;
    UniImageView *view;
} Fixture;

/*************************************************************/
/***** Helpers ***********************************************/
/*************************************************************/

static gchar *
current_name (VnrWindow *window)
{
    g_assert_nonnull (window->file_list);
    return g_path_get_basename (VNR_FILE (window->file_list->data)->path);
}

static void
assert_current (VnrWindow *window, const gchar *expected)
{
    gchar *name = current_name (window);
    g_assert_cmpstr (name, ==, expected);
    g_free (name);
}

static GSList *
paths_of (const gchar *dir, const gchar *name)
{
    return g_slist_append (NULL, g_build_filename (dir, name, NULL));
}

static gboolean
image_shown (gpointer data)
{
    Fixture *fixture = data;

    return fixture->view->pixbuf != NULL
           && gtk_widget_get_mapped (GTK_WIDGET (fixture->view))
           && gtk_widget_get_allocated_width (GTK_WIDGET (fixture->view)) > 1;
}

/* Whether the zoomed image fills the image area along one side and fits
 * in it along the other, as fitting is meant to leave it */
static gboolean
image_fits_snugly (gpointer data)
{
    Fixture *fixture = data;
    GtkAllocation alloc;
    gdouble width = gdk_pixbuf_get_width (fixture->view->pixbuf) * fixture->view->zoom;
    gdouble height = gdk_pixbuf_get_height (fixture->view->pixbuf) * fixture->view->zoom;

    gtk_widget_get_allocation (fixture->window->scroll_view, &alloc);

    return width <= alloc.width + 0.5 && height <= alloc.height + 0.5
           && (width >= alloc.width - 1 || height >= alloc.height - 1);
}

static GtkAction *
find_action (VnrWindow *window, const gchar *name)
{
    GList *it;

    for (it = gtk_ui_manager_get_action_groups (window->ui_mngr); it != NULL; it = it->next)
    {
        GtkAction *action = gtk_action_group_get_action (it->data, name);
        if (action != NULL)
            return action;
    }
    g_assert_not_reached ();
}

static gboolean
press_key (VnrWindow *window, guint keyval, GdkModifierType state)
{
    GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
    GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (window));
    GdkKeymapKey *keys;
    gint n_keys;
    gboolean handled;

    event->key.window = g_object_ref (gtk_widget_get_window (GTK_WIDGET (window)));
    event->key.send_event = TRUE;
    event->key.time = GDK_CURRENT_TIME;
    event->key.keyval = keyval;
    event->key.state = state;

    /* Accelerators are matched by the key, not the keyval */
    g_assert_true (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (display),
                                                      keyval, &keys, &n_keys));
    event->key.hardware_keycode = keys[0].keycode;
    event->key.group = keys[0].group;
    g_free (keys);
    gdk_event_set_device (event, gdk_seat_get_keyboard (gdk_display_get_default_seat (display)));

    handled = gtk_widget_event (GTK_WIDGET (window), event);
    gdk_event_free (event);
    return handled;
}

static const gchar *
message_text (VnrWindow *window)
{
    return gtk_label_get_text (GTK_LABEL (VNR_MESSAGE_AREA (window->msg_area)->message));
}

/* Opens @path in the window of an empty fixture and waits until it shows */
static void
open_and_show (Fixture *fixture, const gchar *path)
{
    GSList *paths = g_slist_append (NULL, g_strdup (path));

    g_assert_true (vnr_window_set_list_from_paths (fixture->window, paths));
    g_slist_free_full (paths, g_free);
    gtk_widget_show (GTK_WIDGET (fixture->window));
    g_assert_true (test_wait_until (image_shown, fixture, TIMEOUT_MS));
}

/*************************************************************/
/***** Fixtures **********************************************/
/*************************************************************/

/* Start each window from the default preferences, rather than those
 * the previous one saved. An empty file, since a missing one is
 * warned about. */
static void
reset_prefs (void)
{
    gchar *path = vnr_prefs_get_config_path ("viewnior.conf");
    gchar *dir = g_path_get_dirname (path);
    GError *error = NULL;

    g_mkdir_with_parents (dir, 0700);
    g_file_set_contents (path, "", -1, &error);
    g_assert_no_error (error);

    g_free (dir);
    g_free (path);
}

static void
new_window (Fixture *fixture)
{
    reset_prefs ();
    fixture->window = VNR_WINDOW (vnr_window_new ());
    fixture->view = UNI_IMAGE_VIEW (fixture->window->view);
    gtk_window_set_default_size (GTK_WINDOW (fixture->window), 480, 300);
}

/* A window like main() opens it for images_dir/@user_data */
static void
fixture_set_up (Fixture *fixture, gconstpointer user_data)
{
    GSList *paths = paths_of (images_dir, user_data);

    new_window (fixture);
    g_assert_true (vnr_window_set_list_from_paths (fixture->window, paths));
    g_slist_free_full (paths, g_free);

    gtk_widget_show (GTK_WIDGET (fixture->window));
    g_assert_true (test_wait_until (image_shown, fixture, TIMEOUT_MS));
}

/* A window with no list, not shown */
static void
fixture_set_up_empty (Fixture *fixture, gconstpointer user_data)
{
    new_window (fixture);
}

static gboolean
destroy_idle (gpointer data)
{
    gtk_widget_destroy (data);
    return G_SOURCE_REMOVE;
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer user_data)
{
    /* The window quits the main loop when it is destroyed */
    g_idle_add (destroy_idle, fixture->window);
    gtk_main ();
}

/*************************************************************/
/***** Loading ***********************************************/
/*************************************************************/

static void
test_load_file (Fixture *fixture, gconstpointer user_data)
{
    GSList *paths = paths_of (images_dir, "b.png");

    g_assert_true (vnr_window_set_list_from_paths (fixture->window, paths));
    assert_current (fixture->window, "b.png");
    g_assert_cmpuint (g_list_length (g_list_first (fixture->window->file_list)), ==, 4);
    g_assert_false (vnr_message_area_is_critical (VNR_MESSAGE_AREA (fixture->window->msg_area)));

    g_slist_free_full (paths, g_free);
}

static void
test_load_not_an_image (Fixture *fixture, gconstpointer user_data)
{
    GSList *paths = paths_of (mixed_dir, "notes.txt");

    /* An error, but the images next to the file can still be browsed */
    g_assert_false (vnr_window_set_list_from_paths (fixture->window, paths));
    g_assert_true (vnr_message_area_is_critical (VNR_MESSAGE_AREA (fixture->window->msg_area)));
    g_assert_nonnull (strstr (message_text (fixture->window), "notes.txt"));

    assert_current (fixture->window, "d.png");
    g_assert_true (vnr_window_next (fixture->window, TRUE));
    assert_current (fixture->window, "e.png");

    g_slist_free_full (paths, g_free);
}

static void
test_load_no_images (Fixture *fixture, gconstpointer user_data)
{
    GSList *paths = g_slist_append (NULL, g_strdup (empty_dir));

    g_assert_false (vnr_window_set_list_from_paths (fixture->window, paths));
    g_assert_true (vnr_message_area_is_critical (VNR_MESSAGE_AREA (fixture->window->msg_area)));
    g_assert_nonnull (strstr (message_text (fixture->window), "contain no images"));
    g_assert_null (fixture->window->file_list);

    g_slist_free_full (paths, g_free);
}

/*************************************************************/
/***** Navigation ********************************************/
/*************************************************************/

static void
test_next_prev (Fixture *fixture, gconstpointer user_data)
{
    VnrWindow *window = fixture->window;

    assert_current (window, "a.png");

    g_assert_true (vnr_window_next (window, TRUE));
    assert_current (window, "b.png");

    /* Both ends wrap around */
    g_assert_true (vnr_window_prev (window));
    g_assert_true (vnr_window_prev (window));
    assert_current (window, "c.png");
    g_assert_true (vnr_window_next (window, TRUE));
    assert_current (window, "a.png");

    vnr_window_last (window);
    assert_current (window, "c.png");
    vnr_window_first (window);
    assert_current (window, "a.png");

    /* The image follows the list */
    g_assert_true (vnr_window_next (window, TRUE));
    g_assert_cmpint (gdk_pixbuf_get_width (fixture->view->pixbuf), ==, SMALL_W);
    g_assert_true (vnr_window_next (window, TRUE));
    assert_current (window, "big.png");
    g_assert_cmpint (gdk_pixbuf_get_width (fixture->view->pixbuf), ==, BIG_W);
}

static void
test_random (Fixture *fixture, gconstpointer user_data)
{
    VnrWindow *window = fixture->window;
    GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    int i;

    for (i = 0; i < 60; i++)
    {
        gchar *before = current_name (window);
        gchar *after;

        g_assert_true (vnr_window_random (window));
        after = current_name (window);

        /* Never the image already shown */
        g_assert_cmpstr (after, !=, before);
        g_hash_table_add (seen, after);
        g_free (before);
    }

    /* Every image can come up */
    g_assert_cmpuint (g_hash_table_size (seen), ==, 4);

    g_hash_table_unref (seen);
}

static void
test_random_single (Fixture *fixture, gconstpointer user_data)
{
    GSList *paths = paths_of (single_dir, "only.png");

    g_assert_true (vnr_window_set_list_from_paths (fixture->window, paths));
    g_assert_false (vnr_window_random (fixture->window));
    assert_current (fixture->window, "only.png");

    g_slist_free_full (paths, g_free);
}

static void
test_random_key (Fixture *fixture, gconstpointer user_data)
{
    VnrWindow *window = fixture->window;

    /* Ctrl+r rotates instead */
    press_key (window, GDK_KEY_r, GDK_CONTROL_MASK);
    assert_current (window, "a.png");
    g_assert_cmpint (gdk_pixbuf_get_width (fixture->view->pixbuf), ==, SMALL_H);
    g_assert_cmpint (gdk_pixbuf_get_height (fixture->view->pixbuf), ==, SMALL_W);

    /* Alt+r is left alone */
    press_key (window, GDK_KEY_r, GDK_MOD1_MASK);
    assert_current (window, "a.png");

    g_assert_true (press_key (window, GDK_KEY_r, 0));
    g_assert_false (window->file_list == g_list_first (window->file_list));
}

/*************************************************************/
/***** Zoom **************************************************/
/*************************************************************/

static void
test_zoom_keys (Fixture *fixture, gconstpointer user_data)
{
    GObject *view = G_OBJECT (fixture->view);

    g_assert_cmpfloat (fixture->view->zoom, ==, 1.0);

    g_assert_true (gtk_bindings_activate (view, GDK_KEY_2, 0));
    g_assert_cmpfloat (fixture->view->zoom, ==, 2.0);
    g_assert_true (gtk_bindings_activate (view, GDK_KEY_3, 0));
    g_assert_cmpfloat (fixture->view->zoom, ==, 3.0);
    g_assert_true (gtk_bindings_activate (view, GDK_KEY_5, 0));
    g_assert_cmpfloat (fixture->view->zoom, ==, 5.0);
    g_assert_true (gtk_bindings_activate (view, GDK_KEY_1, 0));
    g_assert_cmpfloat (fixture->view->zoom, ==, 1.0);
}

static void
test_zoom_steps (Fixture *fixture, gconstpointer user_data)
{
    GObject *view = G_OBJECT (fixture->view);
    int i;

    g_assert_true (gtk_bindings_activate (view, GDK_KEY_plus, 0));
    g_assert_cmpfloat_with_epsilon (fixture->view->zoom, UNI_ZOOM_STEP, 1e-9);
    g_assert_true (gtk_bindings_activate (view, GDK_KEY_minus, 0));
    g_assert_cmpfloat_with_epsilon (fixture->view->zoom, 1.0, 1e-9);

    /* Stops at the limits */
    for (i = 0; i < 100; i++)
        gtk_bindings_activate (view, GDK_KEY_plus, 0);
    g_assert_cmpfloat (fixture->view->zoom, ==, UNI_ZOOM_MAX);

    for (i = 0; i < 200; i++)
        gtk_bindings_activate (view, GDK_KEY_minus, 0);
    g_assert_cmpfloat (fixture->view->zoom, ==, UNI_ZOOM_MIN);
}

static void
test_zoom_50 (Fixture *fixture, gconstpointer user_data)
{
    gtk_action_activate (find_action (fixture->window, "ViewZoom50"));

    g_assert_cmpfloat (fixture->view->zoom, ==, 0.5);
    g_assert_cmpint (fixture->view->fitting, ==, UNI_FITTING_NONE);
}

/* The default zoom mode shrinks large images to the window, and shows
 * small ones at their size */
static void
test_smart_fit_large (Fixture *fixture, gconstpointer user_data)
{
    g_assert_cmpint (fixture->view->fitting, ==, UNI_FITTING_NORMAL);
    g_assert_true (test_wait_until (image_fits_snugly, fixture, TIMEOUT_MS));
    g_assert_cmpfloat (fixture->view->zoom, <, 1.0);
}

static void
test_smart_fit_small (Fixture *fixture, gconstpointer user_data)
{
    g_assert_cmpint (fixture->view->fitting, ==, UNI_FITTING_NORMAL);
    g_assert_cmpfloat (fixture->view->zoom, ==, 1.0);
}

/*************************************************************/
/***** Editing ***********************************************/
/*************************************************************/

static void
test_rotate (Fixture *fixture, gconstpointer user_data)
{
    gtk_action_activate (find_action (fixture->window, "ImageRotateCW"));

    g_assert_cmpint (gdk_pixbuf_get_width (fixture->view->pixbuf), ==, SMALL_H);
    g_assert_cmpint (gdk_pixbuf_get_height (fixture->view->pixbuf), ==, SMALL_W);
}

/* A photo whose EXIF orientation turns it: Viewnior shows it turned, so
 * after rotating and saving, it must still look the way it was saved */
static void
test_save_oriented (Fixture *fixture, gconstpointer user_data)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = test_write_jpeg (dir, "photo.jpg", SMALL_W, SMALL_H);
    GdkPixbuf *saved;
    GdkPixbuf *shown;
    GError *error = NULL;

    /* A quarter turn */
    test_set_orientation (path, 6);

    open_and_show (fixture, path);
    g_assert_cmpint (gdk_pixbuf_get_width (fixture->view->pixbuf), ==, SMALL_H);

    gtk_action_activate (find_action (fixture->window, "ImageRotateCW"));
    g_assert_cmpint (gdk_pixbuf_get_width (fixture->view->pixbuf), ==, SMALL_W);
    gtk_action_activate (find_action (fixture->window, "FileSave"));

    g_assert_cmpint (test_get_orientation (path), ==, 1);

    /* Opened again, as Viewnior and other viewers do */
    saved = gdk_pixbuf_new_from_file (path, &error);
    g_assert_no_error (error);
    shown = gdk_pixbuf_apply_embedded_orientation (saved);
    g_assert_cmpint (gdk_pixbuf_get_width (shown), ==, SMALL_W);
    g_assert_cmpint (gdk_pixbuf_get_height (shown), ==, SMALL_H);

    g_object_unref (shown);
    g_object_unref (saved);
    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

/* After an image fails to open, the previous one is still shown, but
 * editing or saving it must not write it over the failed file */
static void
test_save_after_failed_open (Fixture *fixture, gconstpointer user_data)
{
    /* A PNG signature, so it is listed as an image, then garbage */
    static const gchar broken[] = "\x89PNG\r\n\x1a\nnot really a PNG";
    gchar *dir = test_make_tmp_dir ();
    gchar *good = test_write_png (dir, "a.png", SMALL_W, SMALL_H);
    gchar *bad = g_build_filename (dir, "b.png", NULL);
    gchar *contents;
    gsize length;
    GError *error = NULL;

    g_file_set_contents (bad, broken, sizeof broken - 1, &error);
    g_assert_no_error (error);
    open_and_show (fixture, good);

    gtk_action_activate (find_action (fixture->window, "ImageRotateCW"));
    g_assert_true (gtk_action_is_sensitive (find_action (fixture->window, "FileSave")));

    vnr_window_next (fixture->window, TRUE);
    assert_current (fixture->window, "b.png");

    g_assert_false (gtk_action_is_sensitive (find_action (fixture->window, "FileSave")));
    g_assert_false (gtk_action_is_sensitive (find_action (fixture->window, "ImageRotateCW")));
    gtk_action_activate (find_action (fixture->window, "FileSave"));

    g_file_get_contents (bad, &contents, &length, &error);
    g_assert_no_error (error);
    g_assert_cmpmem (contents, length, broken, sizeof broken - 1);

    g_free (contents);
    g_free (bad);
    g_free (good);
    test_rm_rf (dir);
    g_free (dir);
}

/* gdk-pixbuf reads GIF but can't write it: an edited GIF can't be saved */
static void
test_save_disabled_unwritable (Fixture *fixture, gconstpointer user_data)
{
    /* A 1x1 GIF */
    static const guchar gif[] = {
        'G', 'I', 'F', '8', '9', 'a', 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, ',', 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, ';'
    };
    gchar *dir = test_make_tmp_dir ();
    gchar *path = g_build_filename (dir, "image.gif", NULL);
    GError *error = NULL;

    g_file_set_contents (path, (const gchar *) gif, sizeof gif, &error);
    g_assert_no_error (error);
    open_and_show (fixture, path);
    g_assert_null (fixture->window->writable_format_name);

    gtk_action_activate (find_action (fixture->window, "ImageRotateCW"));

    g_assert_false (gtk_action_is_sensitive (find_action (fixture->window, "FileSave")));
    g_assert_cmpstr (message_text (fixture->window), ==,
                     "Image modifications cannot be saved.\nWriting in this format is not supported.");

    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

/* An image Viewnior can't write is left alone, metadata too: its pixels
 * on disk are still the unturned ones */
static void
test_save_unwritable (Fixture *fixture, gconstpointer user_data)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = test_write_jpeg (dir, "photo.jpg", SMALL_W, SMALL_H);

    test_set_orientation (path, 6);
    open_and_show (fixture, path);

    gtk_action_activate (find_action (fixture->window, "ImageRotateCW"));
    /* As if gdk-pixbuf could read the format but not write it */
    g_clear_pointer (&fixture->window->writable_format_name, g_free);
    gtk_action_activate (find_action (fixture->window, "FileSave"));

    g_assert_cmpint (test_get_orientation (path), ==, 6);
    g_assert_cmpstr (message_text (fixture->window), ==,
                     "Image modifications cannot be saved.\nWriting in this format is not supported.");

    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

/*************************************************************/
/***** Main **************************************************/
/*************************************************************/

static void
make_folders (void)
{
    images_dir = test_make_tmp_dir ();
    g_free (test_write_png (images_dir, "a.png", SMALL_W, SMALL_H));
    g_free (test_write_png (images_dir, "b.png", SMALL_W, SMALL_H));
    g_free (test_write_png (images_dir, "big.png", BIG_W, BIG_H));
    g_free (test_write_png (images_dir, "c.png", SMALL_W, SMALL_H));

    mixed_dir = test_make_tmp_dir ();
    g_free (test_write_png (mixed_dir, "d.png", SMALL_W, SMALL_H));
    g_free (test_write_png (mixed_dir, "e.png", SMALL_W, SMALL_H));
    g_free (test_write_text (mixed_dir, "notes.txt", "not an image\n"));

    single_dir = test_make_tmp_dir ();
    g_free (test_write_png (single_dir, "only.png", SMALL_W, SMALL_H));

    empty_dir = test_make_tmp_dir ();
}

int
main (int argc, char *argv[])
{
    const gchar *skip = g_getenv ("VIEWNIOR_TEST_SKIP");
    gchar *home;
    gchar *dir;
    int result;

    /* Set by meson when it can't give the tests a virtual display */
    if (skip != NULL)
    {
        g_printerr ("%s\n", skip);
        return 77;
    }

    home = test_make_tmp_dir ();

    /* Keep the preferences and keyboard shortcuts the window loads and
     * saves away from the user's own */
    g_setenv ("HOME", home, TRUE);
    dir = g_build_filename (home, "config", NULL);
    g_setenv ("XDG_CONFIG_HOME", dir, TRUE);
    g_free (dir);
    dir = g_build_filename (home, "cache", NULL);
    g_setenv ("XDG_CACHE_HOME", dir, TRUE);
    g_free (dir);

    g_test_init (&argc, &argv, NULL);

    if (!gtk_init_check (&argc, &argv))
    {
        g_printerr ("No display, skipping the window tests\n");
        test_rm_rf (home);
        return 77;
    }

    make_folders ();

#define ADD(path, file, func) \
    g_test_add (path, Fixture, file, fixture_set_up, func, fixture_tear_down)
#define ADD_EMPTY(path, func) \
    g_test_add (path, Fixture, NULL, fixture_set_up_empty, func, fixture_tear_down)

    ADD_EMPTY ("/window/load/file", test_load_file);
    ADD_EMPTY ("/window/load/not-an-image", test_load_not_an_image);
    ADD_EMPTY ("/window/load/no-images", test_load_no_images);

    ADD ("/window/navigation/next-prev", "a.png", test_next_prev);
    ADD ("/window/navigation/random", "a.png", test_random);
    ADD_EMPTY ("/window/navigation/random-single", test_random_single);
    ADD ("/window/navigation/random-key", "a.png", test_random_key);

    ADD ("/window/zoom/keys", "a.png", test_zoom_keys);
    ADD ("/window/zoom/steps", "a.png", test_zoom_steps);
    ADD ("/window/zoom/50", "a.png", test_zoom_50);
    ADD ("/window/zoom/smart-fit-large", "big.png", test_smart_fit_large);
    ADD ("/window/zoom/smart-fit-small", "a.png", test_smart_fit_small);

    ADD ("/window/edit/rotate", "a.png", test_rotate);
    ADD_EMPTY ("/window/edit/save-oriented", test_save_oriented);
    ADD_EMPTY ("/window/edit/save-after-failed-open", test_save_after_failed_open);
    ADD_EMPTY ("/window/edit/save-disabled-unwritable", test_save_disabled_unwritable);
    ADD_EMPTY ("/window/edit/save-unwritable", test_save_unwritable);

    result = g_test_run ();

    test_rm_rf (images_dir);
    test_rm_rf (mixed_dir);
    test_rm_rf (single_dir);
    test_rm_rf (empty_dir);
    test_rm_rf (home);
    return result;
}
