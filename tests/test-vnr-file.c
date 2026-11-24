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

#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include "vnr-file.h"
#include "test-helpers.h"

typedef struct {
    gchar *dir;
    gchar *other_dir;
} Fixture;

/* dir: a.jpg  b2.png  b10.png  .hidden.png  notes.txt  sub/c.png
 * other_dir: z.png */
static void
fixture_set_up (Fixture *fixture, gconstpointer user_data)
{
    gchar *sub;

    fixture->dir = test_make_tmp_dir ();
    g_free (test_write_jpeg (fixture->dir, "a.jpg", 4, 4));
    g_free (test_write_png (fixture->dir, "b2.png", 4, 4));
    g_free (test_write_png (fixture->dir, "b10.png", 4, 4));
    g_free (test_write_png (fixture->dir, ".hidden.png", 4, 4));
    g_free (test_write_text (fixture->dir, "notes.txt", "not an image\n"));

    sub = g_build_filename (fixture->dir, "sub", NULL);
    g_mkdir (sub, 0700);
    g_free (test_write_png (sub, "c.png", 4, 4));
    g_free (sub);

    fixture->other_dir = test_make_tmp_dir ();
    g_free (test_write_png (fixture->other_dir, "z.png", 4, 4));
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer user_data)
{
    test_rm_rf (fixture->dir);
    test_rm_rf (fixture->other_dir);
    g_free (fixture->dir);
    g_free (fixture->other_dir);
}

/* The base names of the whole list @list is a link of, joined by spaces */
static gchar *
list_names (GList *list)
{
    GString *names = g_string_new (NULL);
    GList *it;

    for (it = g_list_first (list); it != NULL; it = it->next)
    {
        gchar *name = g_path_get_basename (VNR_FILE (it->data)->path);

        if (names->len > 0)
            g_string_append_c (names, ' ');
        g_string_append (names, name);
        g_free (name);
    }
    return g_string_free (names, FALSE);
}

static void
assert_list (GList *list, const gchar *expected_names, const gchar *expected_current)
{
    gchar *names = list_names (list);
    gchar *current;

    g_assert_nonnull (list);
    g_assert_cmpstr (names, ==, expected_names);

    current = g_path_get_basename (VNR_FILE (list->data)->path);
    g_assert_cmpstr (current, ==, expected_current);

    g_free (current);
    g_free (names);
}

static void
free_list (GList *list)
{
    g_list_free_full (g_list_first (list), g_object_unref);
}

static void
test_single_file (Fixture *fixture, gconstpointer user_data)
{
    gchar *path = g_build_filename (fixture->dir, "b2.png", NULL);
    GList *list = NULL;
    GError *error = NULL;

    vnr_file_load_single_uri (path, &list, FALSE, &error);

    /* The other images of the folder, in natural order, at the given one */
    g_assert_no_error (error);
    assert_list (list, "a.jpg b2.png b10.png", "b2.png");
    g_assert_cmpstr (VNR_FILE (list->data)->path, ==, path);

    free_list (list);
    g_free (path);
}

static void
test_single_file_hidden (Fixture *fixture, gconstpointer user_data)
{
    gchar *path = g_build_filename (fixture->dir, "b2.png", NULL);
    GList *list = NULL;
    GError *error = NULL;

    vnr_file_load_single_uri (path, &list, TRUE, &error);

    g_assert_no_error (error);
    assert_list (list, ".hidden.png a.jpg b2.png b10.png", "b2.png");

    free_list (list);
    g_free (path);
}

static void
test_single_folder (Fixture *fixture, gconstpointer user_data)
{
    GList *list = NULL;
    GError *error = NULL;

    vnr_file_load_single_uri (fixture->dir, &list, FALSE, &error);

    g_assert_no_error (error);
    assert_list (list, "a.jpg b2.png b10.png", "a.jpg");

    free_list (list);
}

static void
test_single_not_an_image (Fixture *fixture, gconstpointer user_data)
{
    gchar *path = g_build_filename (fixture->dir, "notes.txt", NULL);
    GList *list = NULL;
    GError *error = NULL;

    vnr_file_load_single_uri (path, &list, FALSE, &error);

    /* An error, but the images next to it can still be shown */
    g_assert_nonnull (error);
    g_assert_nonnull (strstr (error->message, "notes.txt"));
    assert_list (list, "a.jpg b2.png b10.png", "a.jpg");

    g_error_free (error);
    free_list (list);
    g_free (path);
}

static void
test_single_missing (Fixture *fixture, gconstpointer user_data)
{
    gchar *path = g_build_filename (fixture->dir, "missing.png", NULL);
    GList *list = NULL;
    GError *error = NULL;

    vnr_file_load_single_uri (path, &list, FALSE, &error);

    g_assert_nonnull (error);
    g_assert_null (list);

    g_error_free (error);
    g_free (path);
}

static void
test_list (Fixture *fixture, gconstpointer user_data)
{
    gchar *z = g_build_filename (fixture->other_dir, "z.png", NULL);
    gchar *b10 = g_build_filename (fixture->dir, "b10.png", NULL);
    gchar *notes = g_build_filename (fixture->dir, "notes.txt", NULL);
    gchar *missing = g_build_filename (fixture->dir, "missing.png", NULL);
    gchar *sub = g_build_filename (fixture->dir, "sub", NULL);
    GSList *paths = NULL;
    GList *list = NULL;
    GError *error = NULL;

    paths = g_slist_append (paths, z);
    paths = g_slist_append (paths, missing);
    paths = g_slist_append (paths, sub);
    paths = g_slist_append (paths, notes);
    paths = g_slist_append (paths, b10);

    vnr_file_load_uri_list (paths, &list, FALSE, &error);

    /* The images given and those in the folders given, sorted together.
     * Missing paths and files that aren't images are skipped. */
    g_assert_no_error (error);
    assert_list (list, "b10.png c.png z.png", "b10.png");

    free_list (list);
    g_slist_free_full (paths, g_free);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

#define ADD(path, func) \
    g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

    ADD ("/file/single/file", test_single_file);
    ADD ("/file/single/file-hidden", test_single_file_hidden);
    ADD ("/file/single/folder", test_single_folder);
    ADD ("/file/single/not-an-image", test_single_not_an_image);
    ADD ("/file/single/missing", test_single_missing);
    ADD ("/file/list", test_list);

    return g_test_run ();
}
