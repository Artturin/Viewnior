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

#include "test-helpers.h"

gint
test_get_orientation (const gchar *path)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    Exiv2::ExifData &exif = image->exifData ();
    auto pos = exif.findKey (Exiv2::ExifKey ("Exif.Image.Orientation"));
    if (pos == exif.end ())
        return -1;
    /* toInt64() is exiv2 0.28 and later, toLong() older */
    return (gint) g_ascii_strtoll (pos->toString ().c_str (), NULL, 10);
}

void
test_set_orientation (const gchar *path, gint orientation)
{
    auto image = Exiv2::ImageFactory::open (path);
    image->readMetadata ();

    image->exifData ()["Exif.Image.Orientation"] = static_cast<uint16_t> (orientation);
    image->writeMetadata ();
}
