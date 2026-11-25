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

#include <string.h>
#include "uni-utils.h"

/**
 * uni_pixbuf_scale_blend:
 *
 * A utility function that either scales or composites color depending
 * on the number of channels in the source image. The last four
 * parameters are only used in the composite color case.
 **/
void
uni_pixbuf_scale_blend (GdkPixbuf * src,
                        GdkPixbuf * dst,
                        int dst_x,
                        int dst_y,
                        int dst_width,
                        int dst_height,
                        gdouble offset_x,
                        gdouble offset_y,
                        gdouble zoom,
                        GdkInterpType interp, int check_x, int check_y)
{
    if (gdk_pixbuf_get_has_alpha (src))
        gdk_pixbuf_composite_color (src, dst,
                                    dst_x, dst_y, dst_width, dst_height,
                                    offset_x, offset_y,
                                    zoom, zoom,
                                    interp,
                                    255,
                                    check_x, check_y,
                                    CHECK_SIZE, CHECK_LIGHT, CHECK_DARK);
    else
        gdk_pixbuf_scale (src, dst,
                          dst_x, dst_y, dst_width, dst_height,
                          offset_x, offset_y, zoom, zoom, interp);
}

void
uni_rectangle_get_rects_around (GdkRectangle * outer,
                                GdkRectangle * inner, GdkRectangle around[4])
{
    /* Top */
    around[0] = (GdkRectangle)
    {
    outer->x, outer->y, outer->width, inner->y - outer->y};
    /* Left */
    around[1] = (GdkRectangle)
    {
    outer->x, inner->y, inner->x - outer->x, inner->height};
    /* Right */
    around[2] = (GdkRectangle)
    {
    inner->x + inner->width,
            inner->y,
            (outer->x + outer->width) - (inner->x + inner->width),
            inner->height};
    /* Bottom */
    around[3] = (GdkRectangle)
    {
    outer->x,
            inner->y + inner->height,
            outer->width,
            (outer->y + outer->height) - (inner->y + inner->height)};
}

/* The value of the environment variable @name, or "" when it is unset,
 * as it can be outside a desktop environment */
static const gchar *
getenv_or_empty (const gchar *name)
{
    const gchar *value = g_getenv (name);

    return value != NULL ? value : "";
}

VnrPrefsDesktop
uni_detect_desktop_environment ()
{
    VnrPrefsDesktop environment = VNR_PREFS_DESKTOP_GNOME3;

    gchar *xdg_current_desktop = g_ascii_strup(getenv_or_empty("XDG_CURRENT_DESKTOP"), -1);
    gchar *xdg_session_desktop = g_ascii_strup(getenv_or_empty("XDG_SESSION_DESKTOP"), -1);
    gchar *desktop_session = g_ascii_strdown(getenv_or_empty("DESKTOP_SESSION"), -1);
    gchar *gdmsession = g_ascii_strdown(getenv_or_empty("GDMSESSION"), -1);

    if (!g_strcmp0(xdg_current_desktop, "GNOME") || !g_strcmp0(xdg_session_desktop, "GNOME"))
    {
        if (!g_strcmp0(gdmsession, "gnome-classic") || !g_strcmp0(gdmsession, "gnome-fallback"))
        {
            environment = VNR_PREFS_DESKTOP_GNOME2;
        }
        else if (!g_strcmp0(gdmsession, "cinnamon"))
        {
            environment = VNR_PREFS_DESKTOP_CINNAMON;
        }
    }
    else if (!g_strcmp0(xdg_current_desktop, "XFCE") || !g_strcmp0(xdg_session_desktop, "XFCE"))
    {
        environment = VNR_PREFS_DESKTOP_XFCE;
    }
    else if (!g_strcmp0(xdg_current_desktop, "MATE") || !g_strcmp0(xdg_session_desktop, "MATE"))
    {
        environment = VNR_PREFS_DESKTOP_MATE;
    }
    else if (!g_strcmp0(xdg_current_desktop, "LXDE") || !g_strcmp0(xdg_session_desktop, "LXDE"))
    {
        environment = VNR_PREFS_DESKTOP_LXDE;
    }
    else if (!g_strcmp0(desktop_session, "fluxbox"))
    {
        environment = VNR_PREFS_DESKTOP_FLUXBOX;
    }
    else
    {
        g_warning("Cannot detect desktop environment. Defaulting to GNOME 3.\n");
    }

    g_free(xdg_current_desktop);
    g_free(xdg_session_desktop);
    g_free(desktop_session);
    g_free(gdmsession);

    return environment;
}

/**
 * uni_xfce_wallpaper_properties:
 * @listing: (nullable): the output of `xfconf-query -c xfce4-desktop -l`
 * @screen_number: the X screen to set the wallpaper on
 * @returns: (transfer full): a %NULL-terminated array of the xfconf
 *           properties holding the wallpaper of each monitor and
 *           workspace of @screen_number
 *
 * XFCE 4.14 and later name each monitor after its connector, as in
 * /backdrop/screen0/monitoreDP-1/workspace0/last-image, so the
 * properties have to be looked up rather than guessed. When @listing
 * has none, which is the case before XFCE saved a wallpaper for the
 * first time, the property XFCE 4.12 uses is returned.
 **/
gchar **
uni_xfce_wallpaper_properties (const gchar *listing, int screen_number)
{
    GPtrArray *props = g_ptr_array_new ();
    gchar *prefix = g_strdup_printf ("/backdrop/screen%d/", screen_number);
    gchar **lines = g_strsplit (listing != NULL ? listing : "", "\n", -1);
    gchar **line;

    for (line = lines; *line != NULL; line++)
    {
        const gchar *prop = g_strstrip (*line);
        gchar **parts;

        if (!g_str_has_prefix (prop, prefix))
            continue;

        /* monitor<name>/workspace<number>/last-image */
        parts = g_strsplit (prop + strlen (prefix), "/", -1);
        if (g_strv_length (parts) == 3
            && g_str_has_prefix (parts[0], "monitor")
            && g_str_has_prefix (parts[1], "workspace")
            && g_strcmp0 (parts[2], "last-image") == 0)
            g_ptr_array_add (props, g_strdup (prop));
        g_strfreev (parts);
    }

    if (props->len == 0)
        g_ptr_array_add (props, g_strdup_printf ("%smonitor0/workspace0/last-image",
                                                 prefix));
    g_ptr_array_add (props, NULL);

    g_strfreev (lines);
    g_free (prefix);
    return (gchar **) g_ptr_array_free (props, FALSE);
}

/**
 * uni_pixbuf_animation_new_static:
 * @pixbuf: the image to show
 * @returns: a new animation with @pixbuf as its only frame
 *
 * Wraps @pixbuf in an animation, for code that only takes animations.
 **/
GdkPixbufAnimation *
uni_pixbuf_animation_new_static (GdkPixbuf *pixbuf)
{
    GdkPixbufSimpleAnim *anim;

    /* Any positive rate: gdk-pixbuf aborts when an iterator of an
     * animation with a negative total time advances. */
    anim = gdk_pixbuf_simple_anim_new (gdk_pixbuf_get_width (pixbuf),
                                       gdk_pixbuf_get_height (pixbuf), 1);
    gdk_pixbuf_simple_anim_add_frame (anim, pixbuf);

    return GDK_PIXBUF_ANIMATION (anim);
}
