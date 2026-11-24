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

#include <exiv2/exiv2.hpp>
#include <glib.h>

#include "uni-exiv2.hpp"
#include "test-helpers.h"

typedef struct {
    gchar *dir;
    gchar *with_exif;
    gchar *without_exif;
    gchar *thumbnail;
} Fixture;

/* Gives @path camera metadata, orientation 6 (a quarter turn) in EXIF
 * and XMP, and @thumbnail as its embedded thumbnail */
static void
write_exif (const gchar *path, const gchar *thumbnail)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    Exiv2::ExifData &exif = image->exifData ();
    exif["Exif.Image.Make"] = "Canon";
    exif["Exif.Image.Model"] = "Canon EOS 40D";
    /* Not Exif.Photo.DateTimeOriginal, the key Viewnior looks up when
     * exiv2 has no dateTimeOriginal() finder */
    exif["Exif.Image.DateTimeOriginal"] = "2008:05:30 15:56:01";
    exif["Exif.Image.Orientation"] = static_cast<uint16_t> (6);
    Exiv2::ExifThumb (exif).setJpegThumbnail (thumbnail);

    image->xmpData ()["Xmp.tiff.Orientation"] = "6";

    image->writeMetadata ();
}

static gchar *
read_xmp_orientation (const gchar *path)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    Exiv2::XmpData &xmp = image->xmpData ();
    auto pos = xmp.findKey (Exiv2::XmpKey ("Xmp.tiff.Orientation"));
    if (pos == xmp.end ())
        return NULL;
    return g_strdup (pos->toString ().c_str ());
}

static gboolean
has_thumbnail (const gchar *path)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    /* An empty MIME type when there is none */
    return Exiv2::ExifThumbC (image->exifData ()).mimeType ()[0] != '\0';
}

static gchar *
read_make (const gchar *path)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    Exiv2::ExifData &exif = image->exifData ();
    auto pos = exif.findKey (Exiv2::ExifKey ("Exif.Image.Make"));
    if (pos == exif.end ())
        return NULL;
    return g_strdup (pos->toString ().c_str ());
}

static void
fixture_set_up (Fixture *fixture, gconstpointer user_data)
{
    fixture->dir = test_make_tmp_dir ();
    fixture->with_exif = test_write_jpeg (fixture->dir, "with-exif.jpg", 8, 8);
    fixture->without_exif = test_write_jpeg (fixture->dir, "without-exif.jpg", 8, 8);
    fixture->thumbnail = test_write_jpeg (fixture->dir, "thumbnail.jpg", 4, 4);
    write_exif (fixture->with_exif, fixture->thumbnail);
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer user_data)
{
    test_rm_rf (fixture->dir);
    g_free (fixture->dir);
    g_free (fixture->with_exif);
    g_free (fixture->without_exif);
    g_free (fixture->thumbnail);
}

static void
collect_cb (const char *label, const char *value, void *user_data)
{
    g_hash_table_insert ((GHashTable *) user_data, g_strdup (label), g_strdup (value));
}

static GHashTable *
read_map (const gchar *path)
{
    GHashTable *map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    uni_read_exiv2_map (path, collect_cb, map);
    return map;
}

static void
test_map (Fixture *fixture, gconstpointer user_data)
{
    GHashTable *map = read_map (fixture->with_exif);

    g_assert_cmpstr ((const char *) g_hash_table_lookup (map, "Camera make"), ==, "Canon");
    g_assert_cmpstr ((const char *) g_hash_table_lookup (map, "Camera model"), ==, "Canon EOS 40D");
#if defined(EXIV2_TEST_VERSION) && EXIV2_TEST_VERSION(0,27,4)
    /* Only found through the dateTimeOriginal() finder */
    g_assert_cmpstr ((const char *) g_hash_table_lookup (map, "Date Taken"), ==, "2008:05:30 15:56:01");
#endif

    g_hash_table_unref (map);
}

static void
test_map_without_exif (Fixture *fixture, gconstpointer user_data)
{
    GHashTable *map = read_map (fixture->without_exif);

    g_assert_cmpuint (g_hash_table_size (map), ==, 0);

    g_hash_table_unref (map);
}

/* Copies the metadata of @source onto the image file at @target, as
 * saving an edited image does. Returns whether that worked. */
static gboolean
copy_metadata (const gchar *source, const gchar *target, gboolean reset_orientation)
{
    gchar *data;
    gsize length;
    unsigned char *result;
    size_t result_length;
    GError *error = NULL;

    g_file_get_contents (target, &data, &length, &error);
    g_assert_no_error (error);
    result = uni_exiv2_copy_metadata (source, (unsigned char *) data, length,
                                      reset_orientation, &result_length);
    g_free (data);
    if (result == NULL)
        return FALSE;

    g_file_set_contents (target, (const gchar *) result, result_length, &error);
    g_assert_no_error (error);
    g_free (result);
    return TRUE;
}

/* Saving a rotated, flipped or cropped image encodes it without metadata,
 * then copies the metadata of the original over */
static void
test_copy_metadata (Fixture *fixture, gconstpointer user_data)
{
    gchar *make;
    gchar *xmp_orientation;

    g_assert_true (copy_metadata (fixture->with_exif, fixture->without_exif, FALSE));

    make = read_make (fixture->without_exif);
    g_assert_cmpstr (make, ==, "Canon");
    g_free (make);

    /* Copied as it is: the pixels weren't turned */
    g_assert_cmpint (test_get_orientation (fixture->without_exif), ==, 6);
    xmp_orientation = read_xmp_orientation (fixture->without_exif);
    g_assert_cmpstr (xmp_orientation, ==, "6");
    g_free (xmp_orientation);
    g_assert_true (has_thumbnail (fixture->without_exif));
}

/* Saving an image whose orientation was applied when it was opened: the
 * saved pixels are upright, so viewers must not turn them again */
static void
test_copy_resets_orientation (Fixture *fixture, gconstpointer user_data)
{
    gchar *make;
    gchar *xmp_orientation;

    g_assert_true (copy_metadata (fixture->with_exif, fixture->without_exif, TRUE));

    g_assert_cmpint (test_get_orientation (fixture->without_exif), ==, 1);
    xmp_orientation = read_xmp_orientation (fixture->without_exif);
    g_assert_cmpstr (xmp_orientation, ==, "1");
    g_free (xmp_orientation);
    /* It still has the old orientation */
    g_assert_false (has_thumbnail (fixture->without_exif));

    /* The rest is copied */
    make = read_make (fixture->without_exif);
    g_assert_cmpstr (make, ==, "Canon");
    g_free (make);
}

/* An image that is upright already keeps its thumbnail */
static void
test_copy_reset_upright (Fixture *fixture, gconstpointer user_data)
{
    test_set_orientation (fixture->with_exif, 1);

    g_assert_true (copy_metadata (fixture->with_exif, fixture->without_exif, TRUE));

    g_assert_cmpint (test_get_orientation (fixture->without_exif), ==, 1);
    g_assert_true (has_thumbnail (fixture->without_exif));
}

/* No orientation is added to an image that had none */
static void
test_copy_reset_without_orientation (Fixture *fixture, gconstpointer user_data)
{
    gchar *copy = test_write_jpeg (fixture->dir, "copy.jpg", 8, 8);

    g_assert_true (copy_metadata (fixture->without_exif, copy, TRUE));

    g_assert_cmpint (test_get_orientation (copy), ==, -1);

    g_free (copy);
}

/* Nothing to copy from a file exiv2 can't read */
static void
test_copy_from_non_image (Fixture *fixture, gconstpointer user_data)
{
    gchar *text = test_write_text (fixture->dir, "notes.txt", "not an image\n");

    g_assert_false (copy_metadata (text, fixture->without_exif, FALSE));

    g_free (text);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

#define ADD(path, func) \
    g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

    ADD ("/exiv2/map", test_map);
    ADD ("/exiv2/map-without-exif", test_map_without_exif);
    ADD ("/exiv2/copy-metadata", test_copy_metadata);
    ADD ("/exiv2/copy-resets-orientation", test_copy_resets_orientation);
    ADD ("/exiv2/copy-reset-upright", test_copy_reset_upright);
    ADD ("/exiv2/copy-reset-without-orientation", test_copy_reset_without_orientation);
    ADD ("/exiv2/copy-from-non-image", test_copy_from_non_image);

    return g_test_run ();
}
