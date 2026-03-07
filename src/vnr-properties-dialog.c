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

#include <libintl.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#define _(String) gettext (String)

#include <time.h>
#include <locale.h>
#include "vnr-properties-dialog.h"
#include "vnr-file.h"
#include "vnr-tools.h"
#include "uni-exiv2.hpp"

G_DEFINE_TYPE (VnrPropertiesDialog, vnr_properties_dialog, GTK_TYPE_DIALOG);

static void vnr_properties_dialog_update_metadata(VnrPropertiesDialog *dialog);

static gboolean
key_pressed_cb (GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                GdkModifierType state, gpointer user_data)
{
    GtkWidget *widget = GTK_WIDGET(user_data);
    if (keyval == GDK_KEY_Escape)
    {
        gtk_widget_hide(widget);
        return TRUE;
    }
    else
        return FALSE;
}

static void
get_file_info(gchar *filename, goffset *size, const gchar **type)
{
    GFile *file;
    GFileInfo *fileinfo;

    file = g_file_new_for_path(filename);
    fileinfo = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE","
                                  G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                  0, NULL, NULL);

    if (fileinfo == NULL)
    {
        return;
    }

    *size = g_file_info_get_size(fileinfo);
    *type = g_strdup(g_file_info_get_content_type(fileinfo));

    g_object_unref(file);
    g_object_unref(fileinfo);
}

static void
set_new_pixbuf(VnrPropertiesDialog *dialog, GdkPixbuf* original)
{
    if(dialog->thumbnail != NULL)
    {
        g_object_unref(dialog->thumbnail);
        dialog->thumbnail = NULL;
    }

    if(original == NULL)
    {
        gtk_image_set_from_icon_name(GTK_IMAGE(dialog->image), "image-missing");
        return;
    }

    int width, height;

    width = gdk_pixbuf_get_width (original);
    height = gdk_pixbuf_get_height (original);

    vnr_tools_fit_to_size(&height, &width, 100,100);

    dialog->thumbnail = gdk_pixbuf_scale_simple (original, width, height,
                                                 GDK_INTERP_NEAREST);
}

static void
vnr_properties_dialog_class_init (VnrPropertiesDialogClass * klass) {}

static void
on_next_clicked (GtkButton *btn, gpointer data)
{
    vnr_window_next (VNR_WINDOW(data), TRUE);
}

static void
on_prev_clicked (GtkButton *btn, gpointer data)
{
    vnr_window_prev (VNR_WINDOW(data));
}

GtkWidget *
vnr_properties_dialog_new (VnrWindow *vnr_win)
{
    VnrPropertiesDialog *dialog;

    dialog = g_object_new (VNR_TYPE_PROPERTIES_DIALOG, NULL);

    dialog->thumbnail = NULL;
    dialog->vnr_win = vnr_win;

    g_signal_connect (dialog->next_button, "clicked",
                      G_CALLBACK(on_next_clicked), vnr_win);
    g_signal_connect (dialog->prev_button, "clicked",
                      G_CALLBACK(on_prev_clicked), vnr_win);

    gtk_button_set_label (GTK_BUTTON(dialog->next_button), _("_Next"));
    gtk_button_set_label (GTK_BUTTON(dialog->prev_button), _("_Previous"));
    gtk_widget_grab_focus (dialog->close_button);

    return (GtkWidget *) dialog;
}


static void
vnr_properties_dialog_init (VnrPropertiesDialog * dialog)
{
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *action_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *temp_box;
    GtkWidget *temp_label;

    gtk_window_set_title(GTK_WINDOW(dialog), _("Image Properties"));
    /* Replaces the GTK3 delete-event signal: hide the window instead of destroying it */
    gtk_window_set_hide_on_close(GTK_WINDOW(dialog), TRUE);

    /* VBox containing the Location labels */
    temp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(temp_box, 10);
    gtk_widget_set_margin_end(temp_box, 10);
    gtk_widget_set_margin_top(temp_box, 10);
    gtk_widget_set_margin_bottom(temp_box, 10);
    gtk_box_append(GTK_BOX(content_area), temp_box);

    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Location:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);

    dialog->location_label = gtk_label_new(NULL);
    gtk_widget_set_halign(dialog->location_label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL(dialog->location_label), TRUE);
    gtk_label_set_ellipsize (GTK_LABEL(dialog->location_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(temp_box), dialog->location_label);

    /* VBox containing the image and meta data */
    dialog->layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(dialog->layout, 10);
    gtk_widget_set_margin_end(dialog->layout, 10);
    gtk_widget_set_margin_top(dialog->layout, 10);
    gtk_widget_set_margin_bottom(dialog->layout, 10);
    gtk_box_append(GTK_BOX(content_area), dialog->layout);

    /* HBox containing the image and the two columns with labels */
    dialog->image_layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(dialog->image_layout, 10);
    gtk_widget_set_margin_end(dialog->image_layout, 10);
    gtk_widget_set_margin_top(dialog->image_layout, 10);
    gtk_widget_set_margin_bottom(dialog->image_layout, 10);
    gtk_box_append(GTK_BOX(dialog->layout), dialog->image_layout);

    /* The frame around the image */
    temp_box = gtk_frame_new(NULL);
    gtk_widget_set_size_request (temp_box, 105, 105);
    gtk_box_append(GTK_BOX(dialog->image_layout), temp_box);

    dialog->image = gtk_image_new_from_icon_name("image-missing");
    gtk_frame_set_child(GTK_FRAME(temp_box), dialog->image);

    /* Image Data Labels */
    temp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(dialog->image_layout), temp_box);

    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Name:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);
    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Type:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);
    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Size:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);
    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Width:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);
    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Height:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);
    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), _("<b>Modified:</b>"));
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), temp_label);

    temp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(dialog->image_layout), temp_box);

    dialog->name_label = gtk_label_new(NULL);
    gtk_label_set_selectable (GTK_LABEL(dialog->name_label), TRUE);
    gtk_widget_set_halign(dialog->name_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), dialog->name_label);
    dialog->type_label = gtk_label_new(NULL);
    gtk_label_set_selectable (GTK_LABEL(dialog->type_label), TRUE);
    gtk_widget_set_halign(dialog->type_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), dialog->type_label);
    dialog->size_label = gtk_label_new(NULL);
    gtk_label_set_selectable (GTK_LABEL(dialog->size_label), TRUE);
    gtk_widget_set_halign(dialog->size_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), dialog->size_label);
    dialog->width_label = gtk_label_new(NULL);
    gtk_label_set_selectable (GTK_LABEL(dialog->width_label), TRUE);
    gtk_widget_set_halign(dialog->width_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), dialog->width_label);
    dialog->height_label = gtk_label_new(NULL);
    gtk_label_set_selectable (GTK_LABEL(dialog->height_label), TRUE);
    gtk_widget_set_halign(dialog->height_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), dialog->height_label);
    dialog->modified_label = gtk_label_new(NULL);
    gtk_label_set_selectable (GTK_LABEL(dialog->modified_label), TRUE);
    gtk_widget_set_halign(dialog->modified_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(temp_box), dialog->modified_label);

    /* Metadata Labels */
    temp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(dialog->layout), temp_box);

    dialog->meta_names_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(temp_box), dialog->meta_names_box);

    dialog->meta_values_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(temp_box), dialog->meta_values_box);

    /* Buttons */
    dialog->prev_button = gtk_button_new_from_icon_name("go-previous");
    gtk_box_append(GTK_BOX(action_area), dialog->prev_button);

    dialog->next_button = gtk_button_new_from_icon_name("go-next");
    gtk_box_append(GTK_BOX(action_area), dialog->next_button);

    dialog->close_button = gtk_button_new_with_mnemonic(_("_Close"));
    gtk_box_append(GTK_BOX(action_area), dialog->close_button);

    gtk_box_append(GTK_BOX(content_area), action_area);

    /* Events and rest */
    g_signal_connect_swapped(G_OBJECT(dialog->close_button), "clicked",
                             G_CALLBACK(gtk_widget_hide), dialog);

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(key_pressed_cb), dialog);
    gtk_widget_add_controller(GTK_WIDGET(dialog), key_ctrl);

    gtk_widget_show(content_area);
}

void
vnr_properties_dialog_update(VnrPropertiesDialog *dialog)
{
    const gchar *filetype = NULL;
    goffset filesize = 0;
    gchar *filetype_desc = NULL;
    gchar *filesize_str = NULL;

    get_file_info ((gchar*)VNR_FILE(dialog->vnr_win->file_list->data)->path,
                   &filesize, &filetype);

    if(filetype == NULL && filesize == 0)
    {
        vnr_properties_dialog_clear(dialog);
        return;
    }

    vnr_properties_dialog_update_image(dialog);
    vnr_properties_dialog_update_metadata(dialog);

    filesize_str = g_format_size (filesize);

    filetype_desc = g_content_type_get_description (filetype);

    gtk_label_set_text(GTK_LABEL(dialog->name_label),
                       (gchar*)VNR_FILE(dialog->vnr_win->file_list->data)->display_name);

    gtk_label_set_text(GTK_LABEL(dialog->location_label),
                       (gchar*)VNR_FILE(dialog->vnr_win->file_list->data)->path);

    gtk_label_set_text(GTK_LABEL(dialog->type_label), filetype_desc);
    gtk_label_set_text(GTK_LABEL(dialog->size_label), filesize_str);

    g_free(filesize_str);
    g_free((gchar*)filetype);
    g_free(filetype_desc);
}

static void
vnr_properties_dialog_clear_metadata(VnrPropertiesDialog *dialog)
{
    GtkWidget *child;

    child = gtk_widget_get_first_child(dialog->meta_values_box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }

    child = gtk_widget_get_first_child(dialog->meta_names_box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }
}

static void
vnr_cb_add_metadata(const char *label, const char *value, void *user_data) {
    VnrPropertiesDialog *dialog = VNR_PROPERTIES_DIALOG(user_data);
    GtkWidget *temp_label;
    gchar *formatted_label;

    // value
    temp_label = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(temp_label), value);
    gtk_label_set_selectable (GTK_LABEL(temp_label), TRUE);
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dialog->meta_values_box), temp_label);

    gtk_widget_show(temp_label);

    // label
    formatted_label = g_strdup_printf("<b>%s:</b>", label);

    temp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(temp_label), formatted_label);
    gtk_widget_set_halign(temp_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dialog->meta_names_box), temp_label);

    g_free(formatted_label);
    gtk_widget_show(temp_label);
}

static void
vnr_properties_dialog_update_metadata(VnrPropertiesDialog *dialog)
{
    vnr_properties_dialog_clear_metadata(dialog);

    uni_read_exiv2_map(
        VNR_FILE(dialog->vnr_win->file_list->data)->path,
        vnr_cb_add_metadata,
        (void*)dialog);
}

void
vnr_properties_dialog_update_image(VnrPropertiesDialog *dialog)
{
    gchar *width_str, *height_str;
    int date_modified_buf_size = 80;
    gchar date_modified[date_modified_buf_size];

    strftime(date_modified,
             date_modified_buf_size * sizeof(gchar),
             "%Ec",
             localtime(&VNR_FILE(dialog->vnr_win->file_list->data)->mtime));
    gtk_label_set_text(GTK_LABEL(dialog->modified_label), date_modified);

    set_new_pixbuf(dialog, uni_image_view_get_pixbuf(UNI_IMAGE_VIEW(dialog->vnr_win->view)));
    gtk_image_set_from_pixbuf (GTK_IMAGE(dialog->image), dialog->thumbnail);

    width_str = g_strdup_printf("%i px", dialog->vnr_win->current_image_width);
    height_str = g_strdup_printf("%i px", dialog->vnr_win->current_image_height);

    gtk_label_set_text(GTK_LABEL(dialog->width_label), width_str);
    gtk_label_set_text(GTK_LABEL(dialog->height_label), height_str);

    g_free(width_str);
    g_free(height_str);
}

void
vnr_properties_dialog_clear(VnrPropertiesDialog *dialog)
{
    set_new_pixbuf(dialog, NULL);
    vnr_properties_dialog_clear_metadata(dialog);

    gtk_label_set_text(GTK_LABEL(dialog->location_label), _("None"));
    gtk_label_set_text(GTK_LABEL(dialog->name_label), _("None"));
    gtk_label_set_text(GTK_LABEL(dialog->type_label), _("None"));
    gtk_label_set_text(GTK_LABEL(dialog->size_label), _("None"));
    gtk_label_set_text(GTK_LABEL(dialog->width_label), _("None"));
    gtk_label_set_text(GTK_LABEL(dialog->height_label), _("None"));
    gtk_label_set_text(GTK_LABEL(dialog->modified_label), _("None"));
}

void
vnr_properties_dialog_show (VnrPropertiesDialog *dialog)
{
    vnr_properties_dialog_update(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}
