/*
 * Copyright © 2009-2018 Siyan Panayotov <contact@siyanpanayotov.com>
 *
 * Based on code by (see README for details):
 * - Björn Lindqvist <bjourne@gmail.com>
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

#include <exiv2/exiv2.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <glib/gi18n.h>

#include "uni-exiv2.hpp"

/* The easy access finders below search the alternative tags a value can be
 * stored under. Older exiv2 lacks them, so look up the exact key there. */
#ifdef EXIV2_TEST_VERSION
    #if EXIV2_TEST_VERSION(0,27,4)
        #define UNI_EXIV2_FINDER_0_27_4(finder) Exiv2::finder
    #endif
#endif
#ifndef UNI_EXIV2_FINDER_0_27_4
    #define UNI_EXIV2_FINDER_0_27_4(finder) NULL
#endif

typedef Exiv2::ExifData::const_iterator (*ExifDataFinder)(const Exiv2::ExifData& ed);

/* Most cameras record the aperture as an f-number, some only as an APEX
 * aperture value. exiv2 prints both as an f-number. */
static Exiv2::ExifData::const_iterator
findAperture(const Exiv2::ExifData& ed)
{
    Exiv2::ExifData::const_iterator pos = Exiv2::fNumber(ed);
    if (pos != ed.end())
        return pos;
#if defined(EXIV2_TEST_VERSION) && EXIV2_TEST_VERSION(0,27,4)
    return Exiv2::apertureValue(ed);
#else
    return ed.findKey(Exiv2::ExifKey("Exif.Photo.ApertureValue"));
#endif
}

typedef struct _ExifDataDictionary ExifDataDictionary;
typedef struct _IptcDataDictionary IptcDataDictionary;

struct _ExifDataDictionary {
    const char *key;
    const char *label;
    ExifDataFinder finder;
};

struct _IptcDataDictionary {
    const char *key;
    const char *label;
};

static ExifDataDictionary exifDataDictionary[] = {
    { "Exif.Image.UniqueCameraModel", N_("Camera make"), Exiv2::make },
    { "Exif.Image.LocalizedCameraModel", N_("Camera model"), Exiv2::model },
    { "Exif.Photo.DateTimeOriginal", N_("Date Taken"), UNI_EXIV2_FINDER_0_27_4(dateTimeOriginal) },
    { "Exif.Photo.ExposureTime", N_("Exposure time"), Exiv2::exposureTime },
    { "Exif.Photo.ExposureMode", N_("Exposure mode"), Exiv2::exposureMode },
    { "Exif.Photo.FNumber", N_("Aperture Value"), findAperture },
    { "Exif.Photo.ISOSpeedRatings", N_("ISO speed"), Exiv2::isoSpeed },
    { "Exif.Photo.Flash", N_("Flash"), UNI_EXIV2_FINDER_0_27_4(flash) },
    { "Exif.Photo.MeteringMode", N_("Metering mode"), Exiv2::meteringMode },
    { "Exif.Image.FocalLength", N_("Focal length"), Exiv2::focalLength },
    { "Exif.Image.Software", N_("Software"), NULL },
    { "Exif.Image.ImageDescription", N_("Image description"), NULL },
    { "Exif.Photo.UserComment", N_("User comment"), NULL }
};

static IptcDataDictionary iptcDataDictionary[] = {
    { "Iptc.Application2.Caption", N_("Description") },
    { "Iptc.Application2.Copyright", N_("Copyright") },
    { "Iptc.Application2.Byline", N_("Author") }
};

#define ARRAY_SIZE(array) (sizeof array/sizeof(array[0]))

#define EXIV_ERROR Exiv2::AnyError
#ifdef EXIV2_VERSION
    #ifdef EXIV2_TEST_VERSION
        #if EXIV2_TEST_VERSION(0,28,0)
            #define EXIV_ERROR Exiv2::Error
        #endif
    #endif
#endif

static std::unique_ptr<Exiv2::Image> cached_image;

/* Formats the GPS latitude or longitude under @key, three rationals for
 * degrees, minutes and seconds, as 52°31'12.0"N, the form map sites
 * accept. Some writers keep fractional minutes and zero seconds, so the
 * value is split again from the total. The letter comes from @ref_key. */
static bool
format_gps_coordinate(const Exiv2::ExifData& ed, const char *key,
                      const char *ref_key, std::string& out)
{
    static const double divisors[] = { 1, 60, 3600 };
    Exiv2::ExifData::const_iterator pos = ed.findKey(Exiv2::ExifKey(key));
    double degrees = 0;

    if (pos == ed.end() || pos->count() != 3)
        return false;

    for (int i = 0; i < 3; i++) {
        Exiv2::Rational r = pos->toRational(i);
        if (r.second == 0 || r.first < 0 || r.second < 0)
            return false;
        degrees += (double) r.first / r.second / divisors[i];
    }

    /* In tenths of a second, so rounding can carry into the minutes and
     * degrees, and no locale's decimal comma gets in */
    long tenths = lround(degrees * 36000);
    gchar *text = g_strdup_printf("%ld°%ld'%ld.%ld\"", tenths / 36000,
                                  tenths / 600 % 60, tenths / 10 % 60, tenths % 10);
    out = text;
    g_free(text);

    Exiv2::ExifData::const_iterator ref = ed.findKey(Exiv2::ExifKey(ref_key));
    if (ref != ed.end())
        out += ref->toString();
    return true;
}

extern "C"
void
uni_read_exiv2_map(const char *uri, void (*callback)(const char*, const char*, void*), void *user_data)
{
    Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);
    try {
        std::unique_ptr<Exiv2::Image> image = Exiv2::ImageFactory::open(uri);
        if (image == nullptr) {
            return;
        }

        image->readMetadata();
        Exiv2::ExifData &exifData = image->exifData();
        Exiv2::IptcData &iptcData = image->iptcData();

        if ( !exifData.empty() ) {
            for ( uint i = 0; i < ARRAY_SIZE(exifDataDictionary); i++ ) {
                ExifDataDictionary dict = exifDataDictionary[i];

                Exiv2::ExifData::const_iterator pos;
                if ( dict.finder == NULL ) {
                    Exiv2::ExifKey key(dict.key);
                    pos = exifData.findKey(key);
                } else {
                    pos = dict.finder(exifData);
                }

                if ( pos != exifData.end() ) {
                    callback(_(dict.label), pos->print(&exifData).c_str(), user_data);
                }
            }

            std::string latitude, longitude;
            if ( format_gps_coordinate(exifData, "Exif.GPSInfo.GPSLatitude",
                                       "Exif.GPSInfo.GPSLatitudeRef", latitude)
                 && format_gps_coordinate(exifData, "Exif.GPSInfo.GPSLongitude",
                                          "Exif.GPSInfo.GPSLongitudeRef", longitude) ) {
                callback(_("GPS coordinates"), (latitude + " " + longitude).c_str(), user_data);
            }
        }

        std::string comment = image->comment();
        if ( ! comment.empty() ) {
            callback( _("Comment"), comment.c_str(), user_data );
        }

        if ( !iptcData.empty() ) {
            for ( uint i = 0; i < ARRAY_SIZE(iptcDataDictionary); i++ ) {
                IptcDataDictionary dict = iptcDataDictionary[i];

                Exiv2::IptcKey key(dict.key);
                Exiv2::IptcData::const_iterator pos;
                pos = iptcData.findKey(key);

                if ( pos != iptcData.end() ) {
                    callback(_(dict.label), pos->value().toString().c_str(), user_data);
                }
            }
        }
    } catch (EXIV_ERROR& e) {
        std::cerr << "Exiv2: '" << e << "'\n";
    }
}

extern "C"
int
uni_read_exiv2_to_cache(const char *uri)
{
    Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);

    if (cached_image != nullptr) {
        cached_image->clearMetadata();
        cached_image.reset(nullptr);
    }

    try {
        cached_image = Exiv2::ImageFactory::open(uri);
        if (cached_image == nullptr) {
            return 1;
        }

        cached_image->readMetadata();
    } catch (EXIV_ERROR& e) {
        std::cerr << "Exiv2: '" << e << "'\n";
    }

    return 0;
}

/* Marks @image as upright. The embedded thumbnail is dropped too: it
 * still has the old orientation, which nothing would apply to it now. */
static void
reset_orientation(Exiv2::Image &image)
{
    Exiv2::ExifData &exif = image.exifData();
    auto pos = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
    if (pos != exif.end() && pos->toString() != "1") {
        pos->setValue("1");
        Exiv2::ExifThumb(exif).erase();
    }

    Exiv2::XmpData &xmp = image.xmpData();
    auto xmp_pos = xmp.findKey(Exiv2::XmpKey("Xmp.tiff.Orientation"));
    if (xmp_pos != xmp.end()) {
        xmp_pos->setValue("1");
    }
}

extern "C"
int
uni_write_exiv2_from_cache(const char *uri, int reset_orientation_tag)
{
    Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);

    if (cached_image == nullptr) {
        return 1;
    }

    try {
        std::unique_ptr<Exiv2::Image> image = Exiv2::ImageFactory::open(uri);
        if (image == nullptr) {
            return 2;
        }

        image->setMetadata( *cached_image );
        if (reset_orientation_tag) {
            reset_orientation(*image);
        }
        image->writeMetadata();

        cached_image->clearMetadata();
        cached_image.reset(nullptr);

        return 0;
    } catch (EXIV_ERROR& e) {
        std::cerr << "Exiv2: '" << e << "'\n";
    }

    return 0;
}
