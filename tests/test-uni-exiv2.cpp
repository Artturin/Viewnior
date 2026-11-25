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

/* Sets each EXIF key in the NULL-terminated @tags, key and value in turn,
 * on @path. Values are parsed as the key's type, "28/10" for a rational. */
static void
write_tags (const gchar *path, const gchar *const *tags)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    for (; tags[0] != NULL; tags += 2)
        image->exifData ()[tags[0]] = std::string (tags[1]);
    image->writeMetadata ();
}

/* The value shown under @label for @path with @tags, or NULL */
static gchar *
map_value (const gchar *path, const gchar *const *tags, const gchar *label)
{
    GHashTable *map;
    gchar *value;

    write_tags (path, tags);
    map = read_map (path);
    value = g_strdup ((const gchar *) g_hash_table_lookup (map, label));
    g_hash_table_unref (map);
    return value;
}

static void
test_map_aperture (Fixture *fixture, gconstpointer user_data)
{
    static const gchar *const f_number[] = { "Exif.Photo.FNumber", "28/10", NULL };
    /* APEX 3 is f/2.8 */
    static const gchar *const apex[] = { "Exif.Photo.ApertureValue", "3/1", NULL };
    gchar *dir = test_make_tmp_dir ();
    gchar *apex_only = test_write_jpeg (dir, "apex.jpg", 8, 8);
    gchar *value;

    value = map_value (fixture->without_exif, f_number, "Aperture Value");
    g_assert_cmpstr (value, ==, "F2.8");
    g_free (value);

    value = map_value (apex_only, apex, "Aperture Value");
    g_assert_cmpstr (value, ==, "F2.8");
    g_free (value);

    g_free (apex_only);
    test_rm_rf (dir);
    g_free (dir);
}

static void
test_map_gps (Fixture *fixture, gconstpointer user_data)
{
    static const gchar *const tags[] = {
        "Exif.GPSInfo.GPSLatitude", "52/1 31/1 1234/100",
        "Exif.GPSInfo.GPSLatitudeRef", "N",
        "Exif.GPSInfo.GPSLongitude", "13/1 24/1 36/1",
        "Exif.GPSInfo.GPSLongitudeRef", "E",
        NULL
    };
    gchar *value = map_value (fixture->without_exif, tags, "GPS coordinates");

    g_assert_cmpstr (value, ==, "52°31'12.3\"N 13°24'36.0\"E");
    g_free (value);
}

/* Some writers keep fractional minutes and zero seconds */
static void
test_map_gps_fractional_minutes (Fixture *fixture, gconstpointer user_data)
{
    static const gchar *const tags[] = {
        "Exif.GPSInfo.GPSLatitude", "33/1 5162/100 0/1",
        "Exif.GPSInfo.GPSLatitudeRef", "S",
        "Exif.GPSInfo.GPSLongitude", "151/1 1263/100 0/1",
        "Exif.GPSInfo.GPSLongitudeRef", "E",
        NULL
    };
    gchar *value = map_value (fixture->without_exif, tags, "GPS coordinates");

    g_assert_cmpstr (value, ==, "33°51'37.2\"S 151°12'37.8\"E");
    g_free (value);
}

/* Seconds that round up to 60 carry into the minutes and degrees */
static void
test_map_gps_rounding (Fixture *fixture, gconstpointer user_data)
{
    static const gchar *const tags[] = {
        "Exif.GPSInfo.GPSLatitude", "10/1 59/1 5996/100",
        "Exif.GPSInfo.GPSLatitudeRef", "N",
        "Exif.GPSInfo.GPSLongitude", "0/1 0/1 0/1",
        "Exif.GPSInfo.GPSLongitudeRef", "W",
        NULL
    };
    gchar *value = map_value (fixture->without_exif, tags, "GPS coordinates");

    g_assert_cmpstr (value, ==, "11°0'0.0\"N 0°0'0.0\"W");
    g_free (value);
}

/* A latitude alone, or a coordinate with a zero denominator, is not shown */
static void
test_map_gps_incomplete (Fixture *fixture, gconstpointer user_data)
{
    static const gchar *const latitude_only[] = {
        "Exif.GPSInfo.GPSLatitude", "52/1 31/1 12/1",
        "Exif.GPSInfo.GPSLatitudeRef", "N",
        NULL
    };
    static const gchar *const broken[] = {
        "Exif.GPSInfo.GPSLongitude", "13/1 24/0 36/1",
        NULL
    };
    gchar *value;

    value = map_value (fixture->without_exif, latitude_only, "GPS coordinates");
    g_assert_null (value);

    value = map_value (fixture->without_exif, broken, "GPS coordinates");
    g_assert_null (value);
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
    gchar *xmp_orientation;

    g_assert_cmpint (uni_read_exiv2_to_cache (fixture->with_exif), ==, 0);
    g_assert_cmpint (uni_write_exiv2_from_cache (fixture->without_exif, FALSE), ==, 0);

    make = read_make (fixture->without_exif);
    g_assert_cmpstr (make, ==, "Canon");
    g_free (make);

    /* Copied as it is: the pixels weren't turned */
    g_assert_cmpint (test_get_orientation (fixture->without_exif), ==, 6);
    xmp_orientation = read_xmp_orientation (fixture->without_exif);
    g_assert_cmpstr (xmp_orientation, ==, "6");
    g_free (xmp_orientation);
    g_assert_true (has_thumbnail (fixture->without_exif));

    /* The cache is used up */
    g_assert_cmpint (uni_write_exiv2_from_cache (fixture->without_exif, FALSE), ==, 1);
}

/* Saving an image whose orientation was applied when it was opened: the
 * saved pixels are upright, so viewers must not turn them again */
static void
test_cache_resets_orientation (Fixture *fixture, gconstpointer user_data)
{
    gchar *make;
    gchar *xmp_orientation;

    g_assert_cmpint (uni_read_exiv2_to_cache (fixture->with_exif), ==, 0);
    g_assert_cmpint (uni_write_exiv2_from_cache (fixture->without_exif, TRUE), ==, 0);

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
test_cache_reset_upright (Fixture *fixture, gconstpointer user_data)
{
    test_set_orientation (fixture->with_exif, 1);

    g_assert_cmpint (uni_read_exiv2_to_cache (fixture->with_exif), ==, 0);
    g_assert_cmpint (uni_write_exiv2_from_cache (fixture->without_exif, TRUE), ==, 0);

    g_assert_cmpint (test_get_orientation (fixture->without_exif), ==, 1);
    g_assert_true (has_thumbnail (fixture->without_exif));
}

/* No orientation is added to an image that had none */
static void
test_cache_reset_without_orientation (Fixture *fixture, gconstpointer user_data)
{
    gchar *copy = g_build_filename (fixture->dir, "copy.jpg", NULL);

    g_free (test_write_jpeg (fixture->dir, "copy.jpg", 8, 8));
    g_assert_cmpint (uni_read_exiv2_to_cache (fixture->without_exif), ==, 0);
    g_assert_cmpint (uni_write_exiv2_from_cache (copy, TRUE), ==, 0);

    g_assert_cmpint (test_get_orientation (copy), ==, -1);

    g_free (copy);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

#define ADD(path, func) \
    g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

    ADD ("/exiv2/map", test_map);
    ADD ("/exiv2/map-without-exif", test_map_without_exif);
    ADD ("/exiv2/map-aperture", test_map_aperture);
    ADD ("/exiv2/map-gps", test_map_gps);
    ADD ("/exiv2/map-gps-fractional-minutes", test_map_gps_fractional_minutes);
    ADD ("/exiv2/map-gps-rounding", test_map_gps_rounding);
    ADD ("/exiv2/map-gps-incomplete", test_map_gps_incomplete);
    ADD ("/exiv2/cache-copies-metadata", test_cache_copies_metadata);
    ADD ("/exiv2/cache-resets-orientation", test_cache_resets_orientation);
    ADD ("/exiv2/cache-reset-upright", test_cache_reset_upright);
    ADD ("/exiv2/cache-reset-without-orientation", test_cache_reset_without_orientation);

    return g_test_run ();
}
