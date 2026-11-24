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

#ifndef __TEST_HELPERS_H__
#define __TEST_HELPERS_H__

#include <glib.h>

G_BEGIN_DECLS

/* A new empty directory under the system temporary directory */
gchar   *test_make_tmp_dir  (void);
/* Deletes @path and, if it is a directory, everything in it */
void     test_rm_rf         (const gchar *path);

/* Write a @width by @height image named @name into @dir. Return its
 * path, to free with g_free(). */
gchar   *test_write_png     (const gchar *dir, const gchar *name, int width, int height);
gchar   *test_write_jpeg    (const gchar *dir, const gchar *name, int width, int height);
gchar   *test_write_text    (const gchar *dir, const gchar *name, const gchar *contents);

/* The EXIF orientation of the image at @path, or -1 if it has none */
gint     test_get_orientation (const gchar *path);
/* Set the EXIF orientation of the image at @path */
void     test_set_orientation (const gchar *path, gint orientation);

/* Run the default main context until @cond returns TRUE. Return FALSE if
 * that takes longer than @timeout_ms. */
gboolean test_wait_until    (gboolean (*cond) (gpointer data), gpointer data, guint timeout_ms);

G_END_DECLS
#endif /* __TEST_HELPERS_H__ */
