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

#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "test-helpers.h"

gchar *
test_make_tmp_dir (void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp ("viewnior-test-XXXXXX", &error);

    g_assert_no_error (error);
    return dir;
}

void
test_rm_rf (const gchar *path)
{
    if (g_file_test (path, G_FILE_TEST_IS_DIR)
        && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
    {
        GDir *dir = g_dir_open (path, 0, NULL);
        const gchar *name;

        while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
        {
            gchar *child = g_build_filename (path, name, NULL);
            test_rm_rf (child);
            g_free (child);
        }
        if (dir != NULL)
            g_dir_close (dir);
    }
    g_remove (path);
}

static gchar *
write_image (const gchar *dir, const gchar *name, int width, int height,
             const gchar *type)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    gchar *path = g_build_filename (dir, name, NULL);

    gdk_pixbuf_fill (pixbuf, 0x3366ccff);
    gdk_pixbuf_save (pixbuf, path, type, &error, NULL);
    g_assert_no_error (error);
    g_object_unref (pixbuf);
    return path;
}

gchar *
test_write_png (const gchar *dir, const gchar *name, int width, int height)
{
    return write_image (dir, name, width, height, "png");
}

gchar *
test_write_jpeg (const gchar *dir, const gchar *name, int width, int height)
{
    return write_image (dir, name, width, height, "jpeg");
}

gchar *
test_write_text (const gchar *dir, const gchar *name, const gchar *contents)
{
    GError *error = NULL;
    gchar *path = g_build_filename (dir, name, NULL);

    g_file_set_contents (path, contents, -1, &error);
    g_assert_no_error (error);
    return path;
}

gboolean
test_wait_until (gboolean (*cond) (gpointer data), gpointer data, guint timeout_ms)
{
    gint64 end = g_get_monotonic_time () + (gint64) timeout_ms * 1000;

    while (!cond (data))
    {
        if (g_get_monotonic_time () > end)
            return FALSE;
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    return TRUE;
}
