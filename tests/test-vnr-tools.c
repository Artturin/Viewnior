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
#include "vnr-tools.h"
#include "uni-utils.h"

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

/* What `xfconf-query -c xfce4-desktop -l` prints on XFCE 4.16 with two
 * monitors on screen 0 and one on screen 1 */
static const gchar *xfce_listing =
    "/backdrop/screen0/monitorHDMI-1/workspace0/color-style\n"
    "/backdrop/screen0/monitorHDMI-1/workspace0/image-style\n"
    "/backdrop/screen0/monitorHDMI-1/workspace0/last-image\n"
    "/backdrop/screen0/monitoreDP-1/workspace0/last-image\n"
    "/backdrop/screen0/monitoreDP-1/workspace1/last-image\n"
    "/backdrop/screen0/monitoreDP-1/last-image\n"
    "/backdrop/screen1/monitorDP-2/workspace0/last-image\n"
    "/backdrop/single-workspace-mode\n"
    "/desktop-icons/style\n";

static void
assert_strv (gchar **actual, const gchar * const *expected)
{
    guint i;

    g_assert_cmpuint (g_strv_length (actual), ==, g_strv_length ((gchar **) expected));
    for (i = 0; expected[i] != NULL; i++)
        g_assert_cmpstr (actual[i], ==, expected[i]);
}

static void
test_xfce_wallpaper_properties (void)
{
    const gchar *screen0[] = {
        "/backdrop/screen0/monitorHDMI-1/workspace0/last-image",
        "/backdrop/screen0/monitoreDP-1/workspace0/last-image",
        "/backdrop/screen0/monitoreDP-1/workspace1/last-image",
        NULL
    };
    const gchar *screen1[] = {
        "/backdrop/screen1/monitorDP-2/workspace0/last-image",
        NULL
    };
    gchar **props;

    props = uni_xfce_wallpaper_properties (xfce_listing, 0);
    assert_strv (props, screen0);
    g_strfreev (props);

    props = uni_xfce_wallpaper_properties (xfce_listing, 1);
    assert_strv (props, screen1);
    g_strfreev (props);
}

/* Before XFCE saved a wallpaper there is nothing to look up */
static void
test_xfce_wallpaper_properties_none (void)
{
    const gchar *fallback0[] = { "/backdrop/screen0/monitor0/workspace0/last-image", NULL };
    const gchar *fallback2[] = { "/backdrop/screen2/monitor0/workspace0/last-image", NULL };
    gchar **props;

    props = uni_xfce_wallpaper_properties (NULL, 0);
    assert_strv (props, fallback0);
    g_strfreev (props);

    props = uni_xfce_wallpaper_properties ("", 0);
    assert_strv (props, fallback0);
    g_strfreev (props);

    props = uni_xfce_wallpaper_properties (xfce_listing, 2);
    assert_strv (props, fallback2);
    g_strfreev (props);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/tools/orientation/rotates", test_orientation_rotates);
    g_test_add_func ("/tools/orientation/can-be-played", test_orientation_can_be_played);
    g_test_add_func ("/tools/orientation/none", test_orientation_none);
    g_test_add_func ("/tools/fit-to-size", test_fit_to_size);
    g_test_add_func ("/utils/xfce-wallpaper-properties", test_xfce_wallpaper_properties);
    g_test_add_func ("/utils/xfce-wallpaper-properties/none", test_xfce_wallpaper_properties_none);

    return g_test_run ();
}
