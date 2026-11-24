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

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif
#include "vnr-tools.h"
#include "uni-utils.h"
#include "test-helpers.h"

/* A static image as vnr_window_open() gets it from a JPEG whose EXIF
 * orientation is @orientation, or without one if it is NULL */
static GdkPixbufAnimation *
new_static_anim (int width, int height, const gchar *orientation)
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    GdkPixbufAnimation *anim;

    if (orientation != NULL)
        g_assert_true (gdk_pixbuf_set_option (pixbuf, "orientation", orientation));

    anim = uni_pixbuf_animation_new_static (pixbuf);
    g_object_unref (pixbuf);
    g_assert_true (gdk_pixbuf_animation_is_static_image (anim));
    return anim;
}

static void
test_orientation_rotates (void)
{
    GdkPixbufAnimation *original = new_static_anim (2, 1, "6");
    GdkPixbufAnimation *anim = g_object_ref (original);

    g_assert_true (vnr_tools_apply_embedded_orientation (&anim));

    /* Orientation 6 is a quarter turn */
    g_assert_true (anim != original);
    g_assert_cmpint (gdk_pixbuf_animation_get_width (anim), ==, 1);
    g_assert_cmpint (gdk_pixbuf_animation_get_height (anim), ==, 2);

    g_object_unref (anim);
    g_object_unref (original);
}

static void
test_orientation_can_be_played (void)
{
    GdkPixbufAnimation *anim = new_static_anim (2, 1, "6");
    GdkPixbufAnimationIter *iter;
    GTimeVal time = { 0, 0 };

    vnr_tools_apply_embedded_orientation (&anim);

    /* p (play) and j (next frame) advance the animation. gdk-pixbuf aborts
     * on that if the rotated image got a negative frame delay. */
    iter = gdk_pixbuf_animation_get_iter (anim, &time);
    time.tv_sec = 10;
    gdk_pixbuf_animation_iter_advance (iter, &time);
    g_assert_cmpint (gdk_pixbuf_get_width (gdk_pixbuf_animation_iter_get_pixbuf (iter)), ==, 1);

    g_object_unref (iter);
    g_object_unref (anim);
}

static void
test_orientation_none (void)
{
    GdkPixbufAnimation *original = new_static_anim (2, 1, NULL);
    GdkPixbufAnimation *anim = original;

    g_assert_false (vnr_tools_apply_embedded_orientation (&anim));

    g_assert_true (anim == original);
    g_assert_cmpint (gdk_pixbuf_animation_get_width (anim), ==, 2);

    g_object_unref (anim);
}

static void
assert_fit (gint width, gint height, gint max_width, gint max_height,
            gint expected_width, gint expected_height)
{
    vnr_tools_fit_to_size (&width, &height, max_width, max_height);
    g_assert_cmpint (width, ==, expected_width);
    g_assert_cmpint (height, ==, expected_height);
}

static void
test_fit_to_size (void)
{
    /* Small enough already: never enlarged */
    assert_fit (50, 40, 100, 100, 50, 40);
    /* Too large: shrunk to the box, keeping the aspect ratio */
    assert_fit (400, 200, 100, 100, 100, 50);
    assert_fit (200, 400, 100, 100, 50, 100);
    assert_fit (300, 300, 100, 50, 50, 50);
    assert_fit (1000, 500, 200, 100, 200, 100);
}

/*************************************************************/
/***** Replacing files ***************************************/
/*************************************************************/

static guint
count_entries (const gchar *dir)
{
    GDir *d = g_dir_open (dir, 0, NULL);
    guint n = 0;

    g_assert_nonnull (d);
    while (g_dir_read_name (d) != NULL)
        n++;
    g_dir_close (d);
    return n;
}

static void
assert_contents (const gchar *path, const gchar *expected)
{
    gchar *contents;
    GError *error = NULL;

    g_file_get_contents (path, &contents, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (contents, ==, expected);
    g_free (contents);
}

static ino_t
inode_of (const gchar *path)
{
    GStatBuf st;

    g_assert_cmpint (g_stat (path, &st), ==, 0);
    return st.st_ino;
}

/* A new file renamed over the old one, with the old one's permissions
 * and extended attributes */
static void
test_replace_file (void)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = test_write_text (dir, "photo.jpg", "old");
    ino_t old_inode = inode_of (path);
    gboolean has_xattr = FALSE;
    GError *error = NULL;
    GStatBuf st;

    g_assert_cmpint (g_chmod (path, 0640), ==, 0);
#ifdef __linux__
    has_xattr = setxattr (path, "user.xdg.tags", "holiday", 7, 0) == 0;
#endif

    g_assert_true (vnr_tools_replace_file (path, "new", 3, &error));
    g_assert_no_error (error);

    assert_contents (path, "new");
    g_assert_cmpint (g_stat (path, &st), ==, 0);
    g_assert_cmpint (st.st_mode & 07777, ==, 0640);
    g_assert_true (st.st_ino != old_inode);
    /* No temporary file left behind */
    g_assert_cmpuint (count_entries (dir), ==, 1);
#ifdef __linux__
    if (has_xattr)
    {
        gchar value[16] = "";
        g_assert_cmpint (getxattr (path, "user.xdg.tags", value, sizeof value - 1), ==, 7);
        g_assert_cmpstr (value, ==, "holiday");
    }
    else
        g_test_message ("No user xattrs here, not checking them");
#endif

    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

/* A symbolic link keeps pointing at the image, which gets the new contents */
static void
test_replace_file_symlink (void)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *target = test_write_text (dir, "photo.jpg", "old");
    gchar *link = g_build_filename (dir, "link.jpg", NULL);
    GError *error = NULL;
    GStatBuf st;

    g_assert_cmpint (symlink ("photo.jpg", link), ==, 0);

    g_assert_true (vnr_tools_replace_file (link, "new", 3, &error));
    g_assert_no_error (error);

    g_assert_cmpint (g_lstat (link, &st), ==, 0);
    g_assert_true (S_ISLNK (st.st_mode));
    assert_contents (target, "new");
    g_assert_cmpuint (count_entries (dir), ==, 2);

    g_free (link);
    g_free (target);
    test_rm_rf (dir);
    g_free (dir);
}

/* Renaming would split hard links, so a file with several is written in
 * place, and every name sees the new contents */
static void
test_replace_file_hard_link (void)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = test_write_text (dir, "photo.jpg", "old");
    gchar *other = g_build_filename (dir, "other.jpg", NULL);
    ino_t old_inode = inode_of (path);
    GError *error = NULL;

    g_assert_cmpint (link (path, other), ==, 0);

    g_assert_true (vnr_tools_replace_file (path, "new", 3, &error));
    g_assert_no_error (error);

    assert_contents (other, "new");
    g_assert_true (inode_of (path) == old_inode);

    g_free (other);
    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

/* Without permission to create a file next to it, the file is written in
 * place, as it could be before */
static void
test_replace_file_read_only_dir (void)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = test_write_text (dir, "photo.jpg", "old");
    ino_t old_inode = inode_of (path);
    GError *error = NULL;

    if (getuid () == 0)
    {
        g_test_skip ("root can write to any directory");
        goto out;
    }
    g_assert_cmpint (g_chmod (dir, 0555), ==, 0);

    g_assert_true (vnr_tools_replace_file (path, "new", 3, &error));
    g_assert_no_error (error);

    assert_contents (path, "new");
    g_assert_true (inode_of (path) == old_inode);

    g_assert_cmpint (g_chmod (dir, 0755), ==, 0);
out:
    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

/* When writing fails, as on a full disk, the old contents stay */
static void
test_replace_file_write_fails (void)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = test_write_text (dir, "photo.jpg", "old");
    gsize length = 64 * 1024;
    gchar *data = g_malloc0 (length);
    struct rlimit old_limit;
    struct rlimit limit;
    GError *error = NULL;
    gboolean replaced;

    /* Files can't grow past 1 KiB: writes beyond fail with EFBIG */
    g_assert_cmpint (getrlimit (RLIMIT_FSIZE, &old_limit), ==, 0);
    limit = old_limit;
    limit.rlim_cur = 1024;
    signal (SIGXFSZ, SIG_IGN);
    g_assert_cmpint (setrlimit (RLIMIT_FSIZE, &limit), ==, 0);

    replaced = vnr_tools_replace_file (path, data, length, &error);

    g_assert_cmpint (setrlimit (RLIMIT_FSIZE, &old_limit), ==, 0);
    signal (SIGXFSZ, SIG_DFL);

    g_assert_false (replaced);
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED);
    assert_contents (path, "old");
    g_assert_cmpuint (count_entries (dir), ==, 1);

    g_clear_error (&error);
    g_free (data);
    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

static void
test_replace_file_missing (void)
{
    gchar *dir = test_make_tmp_dir ();
    gchar *path = g_build_filename (dir, "missing.jpg", NULL);
    GError *error = NULL;

    g_assert_false (vnr_tools_replace_file (path, "new", 3, &error));
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_assert_cmpuint (count_entries (dir), ==, 0);

    g_clear_error (&error);
    g_free (path);
    test_rm_rf (dir);
    g_free (dir);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/tools/orientation/rotates", test_orientation_rotates);
    g_test_add_func ("/tools/orientation/can-be-played", test_orientation_can_be_played);
    g_test_add_func ("/tools/orientation/none", test_orientation_none);
    g_test_add_func ("/tools/fit-to-size", test_fit_to_size);
    g_test_add_func ("/tools/replace-file", test_replace_file);
    g_test_add_func ("/tools/replace-file/symlink", test_replace_file_symlink);
    g_test_add_func ("/tools/replace-file/hard-link", test_replace_file_hard_link);
    g_test_add_func ("/tools/replace-file/read-only-dir", test_replace_file_read_only_dir);
    g_test_add_func ("/tools/replace-file/write-fails", test_replace_file_write_fails);
    g_test_add_func ("/tools/replace-file/missing", test_replace_file_missing);

    return g_test_run ();
}
