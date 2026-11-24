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
} Fixture;

static void
write_exif (const gchar *path)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    Exiv2::ExifData &exif = image->exifData ();
    exif["Exif.Image.Make"] = "Canon";
    exif["Exif.Image.Model"] = "Canon EOS 40D";
    /* Not Exif.Photo.DateTimeOriginal, the key Viewnior looks up when
     * exiv2 has no dateTimeOriginal() finder */
    exif["Exif.Image.DateTimeOriginal"] = "2008:05:30 15:56:01";

    image->writeMetadata ();
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
    write_exif (fixture->with_exif);
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer user_data)
{
    test_rm_rf (fixture->dir);
    g_free (fixture->dir);
    g_free (fixture->with_exif);
    g_free (fixture->without_exif);
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
#if EXIV2_TEST_VERSION(0,27,4)
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

/* Saving a rotated, flipped or cropped image writes a new file without
 * metadata, then copies the metadata of the original over */
static void
test_cache_copies_metadata (Fixture *fixture, gconstpointer user_data)
{
    gchar *make;

    g_assert_cmpint (uni_read_exiv2_to_cache (fixture->with_exif), ==, 0);
    g_assert_cmpint (uni_write_exiv2_from_cache (fixture->without_exif), ==, 0);

    make = read_make (fixture->without_exif);
    g_assert_cmpstr (make, ==, "Canon");
    g_free (make);

    /* The cache is used up */
    g_assert_cmpint (uni_write_exiv2_from_cache (fixture->without_exif), ==, 1);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

#define ADD(path, func) \
    g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

    ADD ("/exiv2/map", test_map);
    ADD ("/exiv2/map-without-exif", test_map_without_exif);
    ADD ("/exiv2/cache-copies-metadata", test_cache_copies_metadata);

    return g_test_run ();
}
