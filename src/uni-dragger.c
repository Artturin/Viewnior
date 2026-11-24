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

#include <math.h>
#include <stdlib.h>
#include "uni-dragger.h"
#include "uni-image-view.h"

G_DEFINE_TYPE (UniDragger, uni_dragger, G_TYPE_OBJECT);


/* Drag 'n Drop */
static GtkTargetEntry target_table[] = {
	{ "text/uri-list", 0, 0},
};

/*************************************************************/
/***** Static stuff ******************************************/
/*************************************************************/

static void
uni_dragger_grab_pointer (UniDragger * tool, GdkEvent * event)
{
    if (tool->grab_cursor == NULL)
        tool->grab_cursor =
            gdk_cursor_new_for_display (gdk_window_get_display (event->any.window),
                                        GDK_FLEUR);

    gdk_seat_grab (gdk_event_get_seat (event), event->any.window,
                   GDK_SEAT_CAPABILITY_ALL_POINTING, FALSE,
                   tool->grab_cursor, event, NULL, NULL);
}

/* On Wayland, GDK keeps showing the grab cursor after an ungrab until the
 * next pointer event, because the window cursor is only looked up again
 * then. Re-setting the toplevel's cursor makes GDK look it up now, so the
 * normal cursor comes back as soon as the button is released, even if the
 * pointer isn't moving. X11 doesn't need this (the server restores it), and
 * there it does nothing visible. */
static void
uni_dragger_refresh_cursor (GdkEvent * event)
{
    GdkWindow *toplevel = gdk_window_get_toplevel (event->any.window);
    GdkCursor *cursor = gdk_window_get_cursor (toplevel);

    /* gdk_window_set_cursor() drops the window's reference before taking
     * the new one, so hold our own across the call. */
    if (cursor != NULL)
        g_object_ref (cursor);
    gdk_window_set_cursor (toplevel, cursor);
    g_clear_object (&cursor);
}

static void
uni_dragger_get_drag_delta (UniDragger * tool, int *x, int *y)
{
    *x = tool->drag_base_x - tool->drag_ofs_x;
    *y = tool->drag_base_y - tool->drag_ofs_y;
}

/*************************************************************/
/***** Actions ***********************************************/
/*************************************************************/

gboolean
uni_dragger_button_press (UniDragger * tool, GdkEventButton * ev)
{
    uni_dragger_grab_pointer (tool, (GdkEvent *) ev);
    tool->pressed = TRUE;
    tool->drag_base_x = ev->x;
    tool->drag_base_y = ev->y;
    tool->drag_ofs_x = ev->x;
    tool->drag_ofs_y = ev->y;

    return TRUE;
}

gboolean
uni_dragger_button_release (UniDragger * tool, GdkEventButton * ev)
{
    if (ev->button != 1)
        return FALSE;
    gdk_seat_ungrab (gdk_event_get_seat ((GdkEvent *) ev));
    uni_dragger_refresh_cursor ((GdkEvent *) ev);
    tool->pressed = FALSE;
    tool->dragging = FALSE;
    return TRUE;
}

gboolean
uni_dragger_motion_notify (UniDragger * tool, GdkEventMotion * ev)
{
    GtkAdjustment * vadj;
    GtkAdjustment * hadj;

    if (tool->pressed)
        tool->dragging = TRUE;
    else
    	return FALSE;

    tool->drag_ofs_x = ev->x;
    tool->drag_ofs_y = ev->y;

    int dx, dy;
    uni_dragger_get_drag_delta (tool, &dx, &dy);
    if (abs (dx) < 1 && abs (dy) < 1)
        return FALSE;

    vadj = uni_image_view_get_vadjustment(UNI_IMAGE_VIEW(tool->view));
    hadj = uni_image_view_get_hadjustment(UNI_IMAGE_VIEW(tool->view));
    if ( pow(dx, 2) + pow(dy, 2) > 7 && UNI_IMAGE_VIEW(tool->view)->pixbuf != NULL &&
    		gtk_adjustment_get_upper(vadj) <= gtk_adjustment_get_page_size(vadj) &&
    		gtk_adjustment_get_upper(hadj) <= gtk_adjustment_get_page_size(hadj) )
    {
        /* End our own grab before handing over to DnD. Don't route this
         * through uni_dragger_button_release(): ev is a motion event, so
         * its ->button check would read unrelated memory. */
        gdk_seat_ungrab (gdk_event_get_seat ((GdkEvent *) ev));
        tool->pressed = FALSE;
        tool->dragging = FALSE;
    	gtk_drag_begin_with_coordinates (GTK_WIDGET(tool->view),
                gtk_target_list_new(target_table, G_N_ELEMENTS(target_table)),
                GDK_ACTION_COPY,
                1,
                (GdkEvent*)ev,
                -1, -1);
		return TRUE;
    }

    GdkRectangle viewport;
    uni_image_view_get_viewport (UNI_IMAGE_VIEW (tool->view), &viewport);

    int offset_x = viewport.x + dx;
    int offset_y = viewport.y + dy;

    uni_image_view_set_offset (UNI_IMAGE_VIEW (tool->view), offset_x,
                               offset_y, FALSE);

    tool->drag_base_x = tool->drag_ofs_x;
    tool->drag_base_y = tool->drag_ofs_y;

    return TRUE;
}

void
uni_dragger_pixbuf_changed (UniDragger * tool,
                            gboolean reset_fit, GdkRectangle * rect)
{
    uni_pixbuf_draw_cache_invalidate (tool->cache);
}

void
uni_dragger_paint_image (UniDragger * tool,
                         UniPixbufDrawOpts * opts, cairo_t *cr)
{
    uni_pixbuf_draw_cache_draw (tool->cache, opts, cr);
}

/*************************************************************/
/***** Stuff that deals with the type ************************/
/*************************************************************/

static void
uni_dragger_finalize (GObject * object)
{
    UniDragger *dragger = UNI_DRAGGER (object);
    uni_pixbuf_draw_cache_free (dragger->cache);
    g_clear_object (&dragger->grab_cursor);

    /* Chain up */
    G_OBJECT_CLASS (uni_dragger_parent_class)->finalize (object);
}

enum {
    PROP_IMAGE_VIEW = 1
};

static void
uni_dragger_set_property (GObject * object,
                          guint prop_id,
                          const GValue * value, GParamSpec * pspec)
{
    UniDragger *dragger = UNI_DRAGGER (object);
    if (prop_id == PROP_IMAGE_VIEW)
        dragger->view = g_value_get_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
uni_dragger_class_init (UniDraggerClass * klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;
    object_class->finalize = uni_dragger_finalize;
    object_class->set_property = uni_dragger_set_property;

    GParamSpec *pspec = g_param_spec_object ("view",
                                             "Image View",
                                             "Image View to navigate",
                                             UNI_TYPE_IMAGE_VIEW,
                                             G_PARAM_CONSTRUCT_ONLY |
                                             G_PARAM_WRITABLE);
    g_object_class_install_property (object_class, PROP_IMAGE_VIEW, pspec);
}

static void
uni_dragger_init (UniDragger * tool)
{
    tool->view = NULL;
    tool->cache = uni_pixbuf_draw_cache_new ();

    tool->pressed = FALSE;
    tool->dragging = FALSE;
    tool->drag_base_x = 0;
    tool->drag_base_y = 0;
    tool->drag_ofs_x = 0;
    tool->drag_ofs_y = 0;
    tool->grab_cursor = NULL;
}

/**
 * uni_dragger_new:
 * @view: a #UniImageView
 * @returns: a new #UniDragger
 *
 * Creates and returns a new dragger tool.
 **/
UniDragger *
uni_dragger_new (GtkWidget * view)
{
    g_return_val_if_fail (view != NULL, NULL);
    UniDragger *data;

    data = UNI_DRAGGER (g_object_new (UNI_TYPE_DRAGGER, "view", view, NULL));

    return data;
}
