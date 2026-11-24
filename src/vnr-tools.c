/*
 * Copyright © 2009-2018 Siyan Panayotov <contact@siyanpanayotov.com>
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

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif
#include "vnr-tools.h"
#include "uni-utils.h"

void
vnr_tools_fit_to_size (gint * width, gint * height, gint max_width, gint max_height)
{
    gdouble w = *width;
    gdouble h = *height;

    vnr_tools_fit_to_size_double (&w, &h, max_width, max_height);

    *width = w;
    *height = h;
}

void
vnr_tools_fit_to_size_double (gdouble * width, gdouble * height, gint max_width, gint max_height)
{
    gdouble ratio, max_ratio;

    /* if size fits well, then exit */
    if (*width < max_width && *height < max_height)
        return;
    /* check if dividing by 0 */
    if (*width == 0 || max_height == 0)
        return;

    ratio = 1. * (*height) / (*width);
    max_ratio = 1. * max_height / max_width;

    if (max_ratio > ratio)
    {
        *width = max_width;
        *height = ratio * (*width);
    }
    else if (ratio > max_ratio)
    {
        *height = max_height;
        *width = (*height) / ratio;
    }
    else
    {
        *width = max_width;
        *height = max_height;
    }

    return;
}

GSList*
vnr_tools_get_list_from_array (gchar **files)
{
    GSList *uri_list = NULL;
    gint i;

    if (files == NULL) return NULL;

    for (i = 0; files[i]; i++) {
        char *uri_string;

        GFile *file;

        file = g_file_new_for_commandline_arg (files[i]);

        uri_string = g_file_get_path (file);

        g_object_unref (file);

        if (uri_string) {
            uri_list = g_slist_prepend (uri_list, g_strdup (uri_string));
            g_free (uri_string);
        }
    }

    return g_slist_reverse (uri_list);
}

/* modified version of eog's
 * eog_util_parse_uri_string_list_to_file_list */
GSList*
vnr_tools_parse_uri_string_list_to_file_list (const gchar *uri_list)
{
    GSList* file_list = NULL;
    gsize i = 0;
    gchar **uris;

    uris = g_uri_list_extract_uris (uri_list);

    while (uris[i] != NULL) {
        gchar* current_path = g_file_get_path (g_file_new_for_uri(uris[i]));
        if(current_path != NULL)
            file_list = g_slist_append (file_list, current_path);
        i++;
    }

    g_strfreev (uris);
    return g_slist_reverse (file_list);
}

gint
compare_quarks (gconstpointer a, gconstpointer b)
{
    GQuark quark;

    quark = g_quark_from_string ((const gchar *) a);

    return quark - GPOINTER_TO_INT (b);
}

void
get_position_of_element_in_list (GList *list, gint *current, gint *total)
{
    GList *it;
    gint after, before;

    after = before = 0;
    it = list;

    for(it = list; it != NULL; it = it->next)
    {
        after ++;
    }

    for(it = list; it != NULL; it = it->prev)
    {
        before ++;
    }

    *current = before;
    *total = before + after - 1;
}

/* Rotates and flips a static image as its embedded EXIF orientation says.
 * Returns whether that changed it. */
gboolean
vnr_tools_apply_embedded_orientation (GdkPixbufAnimation **anim)
{
    GdkPixbuf *pixbuf;
    GdkPixbuf *original;

    if(!gdk_pixbuf_animation_is_static_image (*anim))
        return FALSE;

    pixbuf = gdk_pixbuf_animation_get_static_image (*anim);
    original = pixbuf;
    pixbuf = gdk_pixbuf_apply_embedded_orientation(pixbuf);

    if(original == pixbuf)
    {
        g_object_unref(pixbuf);
        return FALSE;
    }

    g_object_unref(*anim);
    *anim = uni_pixbuf_animation_new_static (pixbuf);
    g_object_unref(pixbuf);
    return TRUE;
}

/* "key-press-event" handler that hides the window on Escape */
gboolean
vnr_tools_hide_on_escape_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if(event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_hide(widget);
        return TRUE;
    }
    else
        return FALSE;
}

static void
set_error_from_errno (GError **error, int saved_errno, const gchar *path)
{
    gchar *display = g_filename_display_name (path);

    g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                 _("Could not write “%s”: %s"), display, g_strerror (saved_errno));
    g_free (display);
}

static gboolean
write_all (int fd, const gchar *data, gsize length)
{
    while (length > 0)
    {
        ssize_t written = write (fd, data, length);

        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        data += written;
        length -= written;
    }
    return TRUE;
}

/* Copies the extended attributes of @from onto @fd: tags and ratings, and
 * on Linux also ACLs and the SELinux label. Best effort, since a filesystem
 * may not take all of them. */
static void
copy_xattrs (const gchar *from, int fd)
{
#ifdef __linux__
    ssize_t size = listxattr (from, NULL, 0);
    gchar *names;
    gchar *name;

    if (size <= 0)
        return;

    names = g_malloc (size);
    size = listxattr (from, names, size);
    for (name = names; size > 0 && name < names + size; name += strlen (name) + 1)
    {
        ssize_t value_size = getxattr (from, name, NULL, 0);
        gchar *value;

        if (value_size < 0)
            continue;
        value = g_malloc (value_size + 1);
        value_size = getxattr (from, name, value, value_size);
        if (value_size >= 0)
            fsetxattr (fd, name, value, value_size, 0);
        g_free (value);
    }
    g_free (names);
#endif
}

/* Writes @data over the file at @path */
static gboolean
replace_in_place (const gchar *path, const gchar *data, gsize length, GError **error)
{
    int fd = g_open (path, O_WRONLY | O_TRUNC, 0);

    if (fd < 0)
    {
        set_error_from_errno (error, errno, path);
        return FALSE;
    }
    if (!write_all (fd, data, length) || fsync (fd) != 0)
    {
        int saved_errno = errno;

        close (fd);
        set_error_from_errno (error, saved_errno, path);
        return FALSE;
    }
    if (close (fd) != 0)
    {
        set_error_from_errno (error, errno, path);
        return FALSE;
    }
    return TRUE;
}

/* Writes @data to a new file next to @path, with the permissions, owner and
 * extended attributes of @path, then renames it over @path. Returns FALSE
 * without setting @error if that can't be done without changing the file's
 * owner, or if no file can be created next to it. */
static gboolean
replace_by_rename (const gchar *path, const GStatBuf *st,
                   const gchar *data, gsize length, GError **error)
{
    gchar *dir = g_path_get_dirname (path);
    gchar *base = g_path_get_basename (path);
    gchar *name = g_strdup_printf (".%s.XXXXXX", base);
    gchar *tmp = g_build_filename (dir, name, NULL);
    gboolean done = FALSE;
    int fd;

    g_free (name);
    g_free (base);
    g_free (dir);

    fd = g_mkstemp_full (tmp, O_WRONLY, 0600);
    if (fd < 0)
    {
        g_free (tmp);
        return FALSE;
    }

    if ((st->st_uid != getuid () || st->st_gid != getgid ())
        && fchown (fd, st->st_uid, st->st_gid) != 0)
    {
        /* Not ours to give away: replacing it would change its owner */
        close (fd);
        g_unlink (tmp);
        g_free (tmp);
        return FALSE;
    }

    if (fchmod (fd, st->st_mode & 07777) != 0
        || !write_all (fd, data, length)
        || fsync (fd) != 0)
    {
        set_error_from_errno (error, errno, path);
        close (fd);
    }
    else
    {
        copy_xattrs (path, fd);
        if (close (fd) != 0)
            set_error_from_errno (error, errno, path);
        else if (g_rename (tmp, path) != 0)
            set_error_from_errno (error, errno, path);
        else
            done = TRUE;
    }

    if (!done)
        g_unlink (tmp);
    g_free (tmp);
    return done;
}

/* Replaces the contents of the file at @path with @data. The new contents
 * go to a new file that is renamed over the old one, so if writing fails
 * the old contents stay. A symbolic link is followed and keeps pointing
 * at the new file. The new file gets the permissions, owner and extended
 * attributes of the old one.
 *
 * Where renaming would change the file in other ways, it is written in
 * place instead, and a failed write can leave it truncated: when it has
 * more than one hard link, when no file can be created in its directory,
 * or when its owner can't be given to a new file. */
gboolean
vnr_tools_replace_file (const gchar *path, const gchar *data, gsize length,
                        GError **error)
{
    gchar *target = realpath (path, NULL);
    GStatBuf st;
    GError *rename_error = NULL;
    gboolean done;

    if (target == NULL)
    {
        set_error_from_errno (error, errno, path);
        return FALSE;
    }
    if (g_stat (target, &st) != 0)
    {
        set_error_from_errno (error, errno, path);
        free (target);
        return FALSE;
    }

    if (st.st_nlink > 1)
        done = replace_in_place (target, data, length, error);
    else if (replace_by_rename (target, &st, data, length, &rename_error))
        done = TRUE;
    else if (rename_error != NULL)
    {
        g_propagate_error (error, rename_error);
        done = FALSE;
    }
    else
        done = replace_in_place (target, data, length, error);

    free (target);
    return done;
}

