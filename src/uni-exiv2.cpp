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
#include <iostream>
#include <memory>

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
    { "Exif.Photo.ApertureValue", N_("Aperture Value"), UNI_EXIV2_FINDER_0_27_4(apertureValue) },
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
unsigned char *
uni_exiv2_copy_metadata(const char *source_path,
                        const unsigned char *data, size_t length,
                        int reset_orientation_tag,
                        size_t *result_length)
{
    Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);

    try {
        auto source = Exiv2::ImageFactory::open(source_path);
        if (source == nullptr) {
            return NULL;
        }
        source->readMetadata();

        auto image = Exiv2::ImageFactory::open(data, length);
        if (image == nullptr) {
            return NULL;
        }
        image->setMetadata(*source);
        if (reset_orientation_tag) {
            reset_orientation(*image);
        }
        image->writeMetadata();

        /* The image was written into its in-memory source */
        Exiv2::BasicIo &io = image->io();
        if (io.open() != 0) {
            return NULL;
        }
        size_t size = (size_t) io.size();
        unsigned char *result = (unsigned char *) g_malloc(size);
        if ((size_t) io.read(result, size) != size) {
            io.close();
            g_free(result);
            return NULL;
        }
        io.close();

        *result_length = size;
        return result;
    } catch (EXIV_ERROR& e) {
        std::cerr << "Exiv2: '" << e << "'\n";
    }

    return NULL;
}
