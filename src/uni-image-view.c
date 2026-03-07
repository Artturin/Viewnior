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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <stdlib.h>

#include "uni-dragger.h"
#include "uni-image-view.h"
#include "uni-dragger.h"
#include "uni-anim-view.h"
#include "uni-marshal.h"
#include "uni-zoom.h"
#include "uni-utils.h"
#include "vnr-window.h"

#define g_signal_handlers_disconnect_by_data(instance, data) \
    g_signal_handlers_disconnect_matched ((instance), G_SIGNAL_MATCH_DATA, \
                                          0, 0, NULL, NULL, (data))
#define g_signal_handlers_block_by_data(instance, data) \
    g_signal_handlers_block_matched ((instance), G_SIGNAL_MATCH_DATA, \
                                     0, 0, NULL, NULL, (data))
#define g_signal_handlers_unblock_by_data(instance, data) \
    g_signal_handlers_unblock_matched ((instance), G_SIGNAL_MATCH_DATA, \
                                       0, 0, NULL, NULL, (data))

/*************************************************************/
/***** Private data ******************************************/
/*************************************************************/
enum {
    SET_ZOOM,
    ZOOM_IN,
    ZOOM_OUT,
    SET_FITTING,
    SCROLL,
    ZOOM_CHANGED,
    PIXBUF_CHANGED,
    LAST_SIGNAL
};

enum {
    P_0,
    P_HADJUSTMENT,
    P_VADJUSTMENT,
    P_HSCROLLPOLICY,
    P_VSCROLLPOLICY
};

struct _UniImageViewPrivate
{
  /* Properties */
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;

  /* GtkScrollablePolicy needs to be checked when
   * driving the scrollable adjustment values */
  GtkScrollablePolicy hscroll_policy : 1;
  GtkScrollablePolicy vscroll_policy : 1;
};

static guint uni_image_view_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE (UniImageView, uni_image_view, GTK_TYPE_WIDGET,
    G_ADD_PRIVATE (UniImageView)
    G_IMPLEMENT_INTERFACE (GTK_TYPE_SCROLLABLE, NULL));

/*************************************************************/
/***** Static stuff ******************************************/
/*************************************************************/

static Size
uni_image_view_get_pixbuf_size (UniImageView * view)
{
    Size s = { 0, 0 };
    if (!view->pixbuf)
        return s;

    s.width = gdk_pixbuf_get_width (view->pixbuf);
    s.height = gdk_pixbuf_get_height (view->pixbuf);
    return s;
}

static Size
uni_image_view_get_allocated_size (UniImageView * view)
{
    GtkAllocation allocation;
    gtk_widget_get_allocation (GTK_WIDGET (view), &allocation);
    Size size = {
        .width = allocation.width,
        .height = allocation.height
    };
    return size;
}

static Size
uni_image_view_get_zoomed_size (UniImageView * view)
{
    Size size = uni_image_view_get_pixbuf_size (view);
    size.width = (int) (size.width * view->zoom + 0.5);
    size.height = (int) (size.height * view->zoom + 0.5);
    return size;
}

static void
uni_image_view_clamp_offset (UniImageView * view, gdouble * x, gdouble * y)
{
    Size alloc = uni_image_view_get_allocated_size (view);
    Size zoomed = uni_image_view_get_zoomed_size (view);

    *x = MIN (*x, zoomed.width - alloc.width);
    *y = MIN (*y, zoomed.height - alloc.height);
    *x = MAX (*x, 0);
    *y = MAX (*y, 0);
}

static void
uni_image_view_update_adjustments (UniImageView * view)
{
    Size zoomed = uni_image_view_get_zoomed_size (view);
    Size alloc = uni_image_view_get_allocated_size (view);

    gtk_adjustment_configure (view->priv->hadjustment,
                              view->offset_x,
                              0.0,
                              zoomed.width,
                              20.0,
                              alloc.width / 2,
                              alloc.width);

    gtk_adjustment_configure (view->priv->vadjustment,
                              view->offset_y,
                              0.0,
                              zoomed.height,
                              20.0,
                              alloc.height / 2,
                              alloc.height);

    g_signal_handlers_block_by_data (G_OBJECT (view->priv->hadjustment), view);
    g_signal_handlers_block_by_data (G_OBJECT (view->priv->vadjustment), view);
    gtk_adjustment_changed (view->priv->hadjustment);
    gtk_adjustment_changed (view->priv->vadjustment);
    g_signal_handlers_unblock_by_data (G_OBJECT (view->priv->hadjustment), view);
    g_signal_handlers_unblock_by_data (G_OBJECT (view->priv->vadjustment), view);
}

/**
 * This method must only be used by uni_image_view_zoom_to_fit () and
 * uni_image_view_set_zoom ().
 **/
static void
uni_image_view_set_zoom_with_center (UniImageView * view,
                                     gdouble zoom,
                                     gdouble center_x,
                                     gdouble center_y, gboolean is_allocating)
{
    gdouble zoom_ratio = zoom / view->zoom;

    Size zoomed = uni_image_view_get_zoomed_size (view);
    Size alloc = uni_image_view_get_allocated_size (view);
    gint x, y;

    x = alloc.width - zoomed.width;
    y = alloc.height - zoomed.height;
    x = (x<0)?0:x;
    y = (y<0)?0:y;

    gdouble offset_x, offset_y;
    offset_x = (view->offset_x + center_x -x/2) * zoom_ratio - center_x;
    offset_y = (view->offset_y + center_y -y/2) * zoom_ratio - center_y;
    view->zoom = zoom;

    uni_image_view_clamp_offset (view, &offset_x, &offset_y);
    view->offset_x = offset_x;
    view->offset_y = offset_y;

    if (!is_allocating && zoom_ratio != 1.0)
    {
        view->fitting = UNI_FITTING_NONE;
        uni_image_view_update_adjustments (view);
        gtk_widget_queue_draw (GTK_WIDGET (view));
    }

    g_signal_emit (G_OBJECT (view),
                   uni_image_view_signals[ZOOM_CHANGED], 0);
}

static void
uni_image_view_set_zoom_no_center (UniImageView * view,
                                   gdouble zoom, gboolean is_allocating)
{
    Size alloc = uni_image_view_get_allocated_size (view);
    gdouble center_x = alloc.width / 2.0;
    gdouble center_y = alloc.height / 2.0;
    uni_image_view_set_zoom_with_center (view, zoom,
                                         center_x, center_y, is_allocating);
}

static void
uni_image_view_zoom_to_fit (UniImageView * view, gboolean is_allocating)
{
    Size img = uni_image_view_get_pixbuf_size (view);
    GtkAllocation alloc;
    VnrWindow *vnr_win = VNR_WINDOW(gtk_widget_get_root (GTK_WIDGET (view)));
    gtk_widget_get_allocation (GTK_WIDGET (vnr_win->scroll_view), &alloc);

    gdouble ratio_x = (gdouble) alloc.width / img.width;
    gdouble ratio_y = (gdouble) alloc.height / img.height;

    gdouble zoom = MIN (ratio_y, ratio_x);

    if (view->fitting == UNI_FITTING_NORMAL)
        zoom = CLAMP (zoom, UNI_ZOOM_MIN, 1.0);
    else if (view->fitting == UNI_FITTING_FULL)
        zoom = CLAMP (zoom, UNI_ZOOM_MIN, UNI_ZOOM_MAX);

    uni_image_view_set_zoom_no_center (view, zoom, is_allocating);
}

static void
uni_image_view_draw_background (UniImageView * view,
                                GdkRectangle * image_area, Size alloc, cairo_t *cr)
{
    GtkWidget *widget = GTK_WIDGET (view);
    int n;
    GdkRGBA rgba;

    cairo_save (cr);
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    if (!gtk_style_context_lookup_color (context, "theme_bg_color", &rgba))
        rgba = (GdkRGBA){0.5, 0.5, 0.5, 1.0};
    gdk_cairo_set_source_rgba (cr, &rgba);

    GdkRectangle borders[4];
    GdkRectangle outer = { 0, 0, alloc.width, alloc.height };
    uni_rectangle_get_rects_around (&outer, image_area, borders);
    for (n = 0; n < 4; n++)
    {
        // Not sure why incrementing the size is necessary.
        borders[n].width++;
        borders[n].height++;
        uni_draw_rect (cr, TRUE, &borders[n]);
    }
    cairo_restore (cr);
}

/**
 * uni_image_view_repaint_area:
 * @paint_rect: The rectangle on the widget that needs to be redrawn.
 *
 * Redraws the porition of the widget defined by @paint_rect.
 **/
static int
uni_image_view_repaint_area (UniImageView * view, GdkRectangle * paint_rect, cairo_t *cr)
{
    if (view->is_rendering)
        return FALSE;

    // Do not draw zero size rectangles.
    if (!paint_rect->width || !paint_rect->height)
        return FALSE;

    view->is_rendering = TRUE;

    // Image area is the area on the widget occupied by the pixbuf.
    GdkRectangle image_area = {0, 0, 0, 0};
    Size alloc = uni_image_view_get_allocated_size (view);
    uni_image_view_get_draw_rect (view, &image_area);
    if (image_area.x > 0 ||
        image_area.y > 0 ||
        image_area.width < alloc.width || image_area.height < alloc.height)
    {
        uni_image_view_draw_background (view, &image_area, alloc, cr);
    }

    // Paint area is the area on the widget that should be redrawn.
    GdkRectangle paint_area = {0, 0, 0, 0};
    gboolean intersects = gdk_rectangle_intersect (&image_area,
                                                   paint_rect,
                                                   &paint_area);
    if (intersects && view->pixbuf)
    {
        int src_x =
            (int) ((view->offset_x + (gdouble) paint_area.x -
                    (gdouble) image_area.x) + 0.5);
        int src_y =
            (int) ((view->offset_y + (gdouble) paint_area.y -
                    (gdouble) image_area.y) + 0.5);

        UniPixbufDrawOpts opts = {
            view->zoom,
            (GdkRectangle) {src_x, src_y,
                            paint_area.width, paint_area.height},
            paint_area.x, paint_area.y,
            view->interp,
            view->pixbuf
        };
        uni_dragger_paint_image (UNI_DRAGGER(view->tool), &opts,
                                 cr);
    }

    view->is_rendering = FALSE;
    return TRUE;
}

static void
uni_image_view_fast_scroll (UniImageView * view, int delta_x, int delta_y)
{
    gtk_widget_queue_draw (GTK_WIDGET (view));
}

static void
uni_image_view_scroll_to (UniImageView * view,
                          gdouble offset_x,
                          gdouble offset_y,
                          gboolean set_adjustments, gboolean invalidate)
{
    int delta_x, delta_y;

    uni_image_view_clamp_offset (view, &offset_x, &offset_y);

    delta_x = floor (offset_x - view->offset_x + 0.5);
    delta_y = floor (offset_y - view->offset_y + 0.5);

    if (delta_x == 0 && delta_y == 0)
        return;

    view->offset_x = offset_x;
    view->offset_y = offset_y;

    if (set_adjustments)
    {
        g_signal_handlers_block_by_data (G_OBJECT (view->priv->hadjustment), view);
        g_signal_handlers_block_by_data (G_OBJECT (view->priv->vadjustment), view);
        gtk_adjustment_set_value (view->priv->hadjustment, view->offset_x);
        gtk_adjustment_set_value (view->priv->vadjustment, view->offset_y);
        g_signal_handlers_unblock_by_data (G_OBJECT (view->priv->hadjustment), view);
        g_signal_handlers_unblock_by_data (G_OBJECT (view->priv->vadjustment), view);
    }

    if (invalidate)
        gtk_widget_queue_draw (GTK_WIDGET (view));
    else
        uni_image_view_fast_scroll (view, delta_x, delta_y);
}

static void
uni_image_view_scroll (UniImageView * view,
                       GtkScrollType xscroll, GtkScrollType yscroll)
{
    GtkAdjustment *hadj = view->priv->hadjustment;
    GtkAdjustment *vadj = view->priv->vadjustment;

    gdouble h_step = gtk_adjustment_get_step_increment (hadj);
    gdouble v_step = gtk_adjustment_get_step_increment (vadj);
    gdouble h_page = gtk_adjustment_get_page_increment (hadj);
    gdouble v_page = gtk_adjustment_get_page_increment (vadj);

    int xstep = 0;
    if (xscroll == GTK_SCROLL_STEP_LEFT)
        xstep = -h_step;
    else if (xscroll == GTK_SCROLL_STEP_RIGHT)
        xstep = h_step;
    else if (xscroll == GTK_SCROLL_PAGE_LEFT)
        xstep = -h_page;
    else if (xscroll == GTK_SCROLL_PAGE_RIGHT)
        xstep = h_page;

    int ystep = 0;
    if (yscroll == GTK_SCROLL_STEP_UP)
        ystep = -v_step;
    else if (yscroll == GTK_SCROLL_STEP_DOWN)
        ystep = v_step;
    else if (yscroll == GTK_SCROLL_PAGE_UP)
        ystep = -v_page;
    else if (yscroll == GTK_SCROLL_PAGE_DOWN)
        ystep = v_page;
    
    uni_image_view_scroll_to (view,
                              view->offset_x + xstep,
                              view->offset_y + ystep, TRUE, FALSE);
}

/*************************************************************/
/***** Private signal handlers *******************************/
/*************************************************************/
static void
uni_image_view_unrealize (GtkWidget * widget)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    if (view->void_cursor) {
        g_object_unref (view->void_cursor);
        view->void_cursor = NULL;
    }
    GTK_WIDGET_CLASS (uni_image_view_parent_class)->unrealize (widget);
}

static void
uni_image_view_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);

    if (view->pixbuf && view->fitting != UNI_FITTING_NONE)
        uni_image_view_zoom_to_fit (view, TRUE);

    uni_image_view_clamp_offset (view, &view->offset_x, &view->offset_y);
    uni_image_view_update_adjustments (view);
}

static void
uni_image_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
    GtkAllocation allocation;
    GtkWidget *scroll_win = VNR_WINDOW(gtk_widget_get_root (widget))->scroll_view;
    gtk_widget_get_allocation (GTK_WIDGET (scroll_win), &allocation);
    allocation.x = 0;
    allocation.y = 0;

    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, (float)allocation.width, (float)allocation.height);
    cairo_t *cr = gtk_snapshot_append_cairo (snapshot, &bounds);
    GdkRectangle rect = {0, 0, allocation.width, allocation.height};
    uni_image_view_repaint_area (UNI_IMAGE_VIEW (widget), &rect, cr);
    cairo_destroy (cr);
}

static void
uni_image_view_pressed_cb (GtkGestureClick *gesture, int n_press, double x, double y, UniImageView *view)
{
    gtk_widget_grab_focus (GTK_WIDGET (view));
    VnrWindow *vnr_win = VNR_WINDOW(gtk_widget_get_root (GTK_WIDGET (view)));
    guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

    if (n_press == 2 && button == 1 && vnr_win->prefs->behavior_click == VNR_PREFS_CLICK_FULLSCREEN)
    {
        vnr_window_toggle_fullscreen (vnr_win);
        return;
    }
    else if (n_press == 2 && button == 1 && vnr_win->prefs->behavior_click == VNR_PREFS_CLICK_NEXT)
    {
        GtkAllocation alloc;
        gtk_widget_get_allocation (GTK_WIDGET (view), &alloc);
        if (x / alloc.width < 0.5)
            vnr_window_prev (vnr_win);
        else
            vnr_window_next (vnr_win, TRUE);
        return;
    }
    else if (n_press == 1 && button == 1)
    {
        uni_dragger_button_press (UNI_DRAGGER(view->tool), x, y);
        return;
    }
    else if (n_press == 2 && button == 1)
    {
        if (view->fitting == UNI_FITTING_FULL ||
            (view->fitting == UNI_FITTING_NORMAL && view->zoom != 1.0))
            uni_image_view_set_zoom_with_center (view, 1., x, y, FALSE);
        else
            uni_image_view_set_fitting (view, UNI_FITTING_FULL);
        return;
    }
    else if (n_press == 1 && button == 3)
    {
        vnr_window_show_popup_menu (vnr_win);
        return;
    }
    else if (n_press == 1 && button == 8)
    {
        vnr_window_prev (vnr_win);
    }
    else if (n_press == 1 && button == 9)
    {
        vnr_window_next (vnr_win, TRUE);
    }
}

static void
uni_image_view_released_cb (GtkGestureClick *gesture, int n_press, double x, double y, UniImageView *view)
{
    guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
    if (button == 1)
        uni_dragger_button_release (UNI_DRAGGER(view->tool));
}

static void
uni_image_view_motion_cb (GtkEventControllerMotion *ctrl, double x, double y, UniImageView *view)
{
    if (view->is_rendering)
        return;
    uni_dragger_motion_notify (UNI_DRAGGER(view->tool), x, y);
}

static gboolean
uni_image_view_scroll_cb (GtkEventControllerScroll *ctrl, double dx, double dy, UniImageView *view)
{
    gdouble zoom;
    VnrWindow *vnr_win = VNR_WINDOW(gtk_widget_get_root (GTK_WIDGET (view)));
    GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (ctrl));

    GtkAllocation alloc;
    gtk_widget_get_allocation (GTK_WIDGET (view), &alloc);
    double px = alloc.width / 2.0;
    double py = alloc.height / 2.0;

    if (vnr_win->prefs->behavior_wheel == VNR_PREFS_WHEEL_ZOOM || (state & GDK_CONTROL_MASK) != 0)
    {
        if (dx < 0) { vnr_window_prev (vnr_win); }
        else if (dx > 0) { vnr_window_next (vnr_win, TRUE); }
        else if (dy < 0)
        {
            if (state & GDK_SHIFT_MASK) vnr_window_prev (vnr_win);
            else {
                zoom = CLAMP (view->zoom * UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX);
                uni_image_view_set_zoom_with_center (view, zoom, px, py, FALSE);
            }
        }
        else
        {
            if (state & GDK_SHIFT_MASK) vnr_window_next (vnr_win, TRUE);
            else {
                zoom = CLAMP (view->zoom / UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX);
                uni_image_view_set_zoom_with_center (view, zoom, px, py, FALSE);
            }
        }
    }
    else if (vnr_win->prefs->behavior_wheel == VNR_PREFS_WHEEL_NAVIGATE)
    {
        if (dx < 0) { zoom = CLAMP (view->zoom * UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX); uni_image_view_set_zoom_with_center (view, zoom, px, py, FALSE); }
        else if (dx > 0) { zoom = CLAMP (view->zoom / UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX); uni_image_view_set_zoom_with_center (view, zoom, px, py, FALSE); }
        else if (dy < 0)
        {
            if (state & GDK_SHIFT_MASK) { zoom = CLAMP (view->zoom * UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX); uni_image_view_set_zoom_with_center (view, zoom, px, py, FALSE); }
            else vnr_window_prev (vnr_win);
        }
        else
        {
            if (state & GDK_SHIFT_MASK) { zoom = CLAMP (view->zoom / UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX); uni_image_view_set_zoom_with_center (view, zoom, px, py, FALSE); }
            else vnr_window_next (vnr_win, TRUE);
        }
    }
    else
    {
        if (dx < 0) uni_image_view_scroll (view, GTK_SCROLL_PAGE_LEFT, GTK_SCROLL_NONE);
        else if (dx > 0) uni_image_view_scroll (view, GTK_SCROLL_PAGE_RIGHT, GTK_SCROLL_NONE);
        else if (dy < 0)
        {
            if (state & GDK_SHIFT_MASK) uni_image_view_scroll (view, GTK_SCROLL_PAGE_LEFT, GTK_SCROLL_NONE);
            else uni_image_view_scroll (view, GTK_SCROLL_NONE, GTK_SCROLL_PAGE_UP);
        }
        else
        {
            if (state & GDK_SHIFT_MASK) uni_image_view_scroll (view, GTK_SCROLL_PAGE_RIGHT, GTK_SCROLL_NONE);
            else uni_image_view_scroll (view, GTK_SCROLL_NONE, GTK_SCROLL_PAGE_DOWN);
        }
    }
    return TRUE;
}

static gboolean
uni_image_view_hadj_changed_cb (GtkAdjustment * adj, UniImageView * view)
{
    int offset_x;
    offset_x = gtk_adjustment_get_value (adj);
    uni_image_view_scroll_to (view, offset_x, view->offset_y, FALSE, FALSE);
    return FALSE;
}

static gboolean
uni_image_view_vadj_changed_cb (GtkAdjustment * adj, UniImageView * view)
{
    int offset_y;
    offset_y = gtk_adjustment_get_value (adj);
    uni_image_view_scroll_to (view, view->offset_x, offset_y, FALSE, FALSE);
    return FALSE;
}

static void
uni_image_view_set_scroll_adjustments (UniImageView * view,
                                       GtkAdjustment * hadj,
                                       GtkAdjustment * vadj)
{
    if (hadj && (view->priv->hadjustment != hadj))
    {
        if (view->priv->hadjustment)
        {
            g_signal_handlers_disconnect_by_data (G_OBJECT (view->priv->hadjustment), view);
            g_object_unref (view->priv->hadjustment);
        }
        g_signal_connect (hadj,
                          "value_changed",
                          G_CALLBACK(uni_image_view_hadj_changed_cb), view);
        view->priv->hadjustment = hadj;
        g_object_ref_sink (view->priv->hadjustment);
    }
    if (vadj && (view->priv->vadjustment != vadj))
    {
        if (view->priv->vadjustment)
        {
            g_signal_handlers_disconnect_by_data (G_OBJECT (view->priv->vadjustment), view);
            g_object_unref (view->priv->vadjustment);
        }
        g_signal_connect (vadj,
                          "value_changed",
                          G_CALLBACK(uni_image_view_vadj_changed_cb), view);
        view->priv->vadjustment = vadj;
        g_object_ref_sink (view->priv->vadjustment);
    }
}


/*************************************************************/
/***** Stuff that deals with the type ************************/
/*************************************************************/
static void
uni_image_view_init (UniImageView * view)
{
    gtk_widget_set_can_focus (GTK_WIDGET(view), TRUE);

    view->interp = GDK_INTERP_BILINEAR;
    view->fitting = UNI_FITTING_NORMAL;
    view->pixbuf = NULL;
    view->zoom = 1.0;
    view->offset_x = 0.0;
    view->offset_y = 0.0;
    view->is_rendering = FALSE;
    view->show_cursor = TRUE;
    view->void_cursor = gdk_cursor_new_from_name ("default", NULL);
    view->tool = G_OBJECT (uni_dragger_new ((GtkWidget *) view));

    view->priv = uni_image_view_get_instance_private (view);

    view->priv->hadjustment = view->priv->vadjustment = NULL;
    uni_image_view_set_scroll_adjustments(view, GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 1.0, 0.0,
                                          1.0, 1.0, 1.0)), GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 1.0, 0.0,
                                          1.0, 1.0, 1.0)));
    g_object_ref_sink (view->priv->hadjustment);
    g_object_ref_sink (view->priv->vadjustment);

    GtkGesture *click = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
    g_signal_connect (click, "pressed", G_CALLBACK (uni_image_view_pressed_cb), view);
    g_signal_connect (click, "released", G_CALLBACK (uni_image_view_released_cb), view);
    gtk_widget_add_controller (GTK_WIDGET (view), GTK_EVENT_CONTROLLER (click));

    GtkEventController *motion = gtk_event_controller_motion_new ();
    g_signal_connect (motion, "motion", G_CALLBACK (uni_image_view_motion_cb), view);
    gtk_widget_add_controller (GTK_WIDGET (view), motion);

    GtkEventController *scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect (scroll, "scroll", G_CALLBACK (uni_image_view_scroll_cb), view);
    gtk_widget_add_controller (GTK_WIDGET (view), scroll);
}

static void
uni_image_view_finalize (GObject * object)
{
    UniImageView *view = UNI_IMAGE_VIEW (object);
    if (view->priv->hadjustment)
    {
        g_signal_handlers_disconnect_by_data (G_OBJECT (view->priv->hadjustment), view);
        g_object_unref (view->priv->hadjustment);
        view->priv->hadjustment = NULL;
    }
    if (view->priv->vadjustment)
    {
        g_signal_handlers_disconnect_by_data (G_OBJECT (view->priv->vadjustment), view);
        g_object_unref (view->priv->vadjustment);
        view->priv->vadjustment = NULL;
    }
    if (view->pixbuf)
    {
        g_object_unref (view->pixbuf);
        view->pixbuf = NULL;
    }
    g_object_unref (view->tool);
    /* Chain up. */
    G_OBJECT_CLASS (uni_image_view_parent_class)->finalize (object);
}

static void
uni_image_view_init_signals (UniImageViewClass * klass)
{
    uni_image_view_signals[SET_ZOOM] =
        g_signal_new ("set_zoom",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UniImageViewClass, set_zoom),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__DOUBLE,
                      G_TYPE_NONE, 1, G_TYPE_DOUBLE);
    uni_image_view_signals[ZOOM_IN] =
        g_signal_new ("zoom_in",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UniImageViewClass, zoom_in),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    uni_image_view_signals[ZOOM_OUT] =
        g_signal_new ("zoom_out",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UniImageViewClass, zoom_out),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    uni_image_view_signals[SET_FITTING] =
        g_signal_new ("set_fitting",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UniImageViewClass, set_fitting),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__ENUM,
                      G_TYPE_NONE, 1, G_TYPE_INT);
    uni_image_view_signals[SCROLL] =
        g_signal_new ("scroll",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (UniImageViewClass, scroll),
                      NULL, NULL,
                      uni_marshal_VOID__ENUM_ENUM,
                      G_TYPE_NONE,
                      2, GTK_TYPE_SCROLL_TYPE, GTK_TYPE_SCROLL_TYPE);
    uni_image_view_signals[ZOOM_CHANGED] =
        g_signal_new ("zoom_changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    /**
     * UniImageView::pixbuf-changed:
     * @view: The view that emitted the signal.
     *
     * The ::pixbuf-changed signal is emitted when the pixbuf the
     * image view shows is changed and when its image data is changed.
     * Listening to this signal is useful if you, for example, have a
     * label that displays the width and height of the pixbuf in the
     * view.
     **/
    uni_image_view_signals[PIXBUF_CHANGED] =
        g_signal_new ("pixbuf_changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (UniImageViewClass, pixbuf_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
uni_image_view_get_property (GObject     *object,
			 guint        prop_id,
			 GValue      *value,
			 GParamSpec  *pspec)
{
  UniImageView *iv = UNI_IMAGE_VIEW (object);
  UniImageViewPrivate *priv = iv->priv;

  switch (prop_id)
    {
    case P_HADJUSTMENT:
      g_value_set_object (value, priv->hadjustment);
      break;
    case P_VADJUSTMENT:
      g_value_set_object (value, priv->vadjustment);
      break;
    case P_HSCROLLPOLICY:
      g_value_set_enum (value, priv->hscroll_policy);
      break;
    case P_VSCROLLPOLICY:
      g_value_set_enum (value, priv->vscroll_policy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
uni_image_view_set_property (GObject      *object,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
  UniImageView *iv = UNI_IMAGE_VIEW (object);
  UniImageViewPrivate *priv = iv->priv;

  switch (prop_id)
    {
    case P_HADJUSTMENT:
      uni_image_view_set_hadjustment (iv, 
				  (GtkAdjustment*) g_value_get_object (value));
      break;
    case P_VADJUSTMENT:
      uni_image_view_set_vadjustment (iv, 
				  (GtkAdjustment*) g_value_get_object (value));
      break;
    case P_HSCROLLPOLICY:
      priv->hscroll_policy = g_value_get_enum (value);
      gtk_widget_queue_resize (GTK_WIDGET (iv));
      break;
    case P_VSCROLLPOLICY:
      priv->vscroll_policy = g_value_get_enum (value);
      gtk_widget_queue_resize (GTK_WIDGET (iv));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
uni_image_view_class_init (UniImageViewClass * klass)
{
    uni_image_view_init_signals (klass);

    GObjectClass *object_class = (GObjectClass *) klass;
    object_class->finalize = uni_image_view_finalize;

    GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;
    widget_class->snapshot = uni_image_view_snapshot;
    widget_class->size_allocate = uni_image_view_size_allocate;
    widget_class->unrealize = uni_image_view_unrealize;
    object_class->set_property = uni_image_view_set_property;
    object_class->get_property = uni_image_view_get_property;

    klass->set_zoom = uni_image_view_set_zoom;
    klass->zoom_in = uni_image_view_zoom_in;
    klass->zoom_out = uni_image_view_zoom_out;
    klass->set_fitting = uni_image_view_set_fitting;
    klass->scroll = uni_image_view_scroll;
    klass->pixbuf_changed = NULL;

    g_object_class_override_property (object_class, P_HADJUSTMENT, "hadjustment");
    g_object_class_override_property (object_class, P_VADJUSTMENT, "vadjustment");
    g_object_class_override_property (object_class, P_HSCROLLPOLICY, "hscroll-policy");
    g_object_class_override_property (object_class, P_VSCROLLPOLICY, "vscroll-policy");
    
    /* Set up scrolling.*/
    klass->set_scroll_adjustments = uni_image_view_set_scroll_adjustments;

    /* Add keybindings. */
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_1, 0, "set_zoom", "(d)", 1.0);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_2, 0, "set_zoom", "(d)", 2.0);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_3, 0, "set_zoom", "(d)", 3.0);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_1, 0, "set_zoom", "(d)", 1.0);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_2, 0, "set_zoom", "(d)", 2.0);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_3, 0, "set_zoom", "(d)", 3.0);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_plus, 0, "zoom_in", NULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_equal, 0, "zoom_in", NULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_Add, 0, "zoom_in", NULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_minus, 0, "zoom_out", NULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_Subtract, 0, "zoom_out", NULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_f, 0, "set_fitting", "(i)", (int)UNI_FITTING_FULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_0, 0, "set_fitting", "(i)", (int)UNI_FITTING_FULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_0, 0, "set_fitting", "(i)", (int)UNI_FITTING_FULL);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Right, 0, "scroll", "(ii)", (int)GTK_SCROLL_STEP_RIGHT, (int)GTK_SCROLL_NONE);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Left, 0, "scroll", "(ii)", (int)GTK_SCROLL_STEP_LEFT, (int)GTK_SCROLL_NONE);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Down, 0, "scroll", "(ii)", (int)GTK_SCROLL_NONE, (int)GTK_SCROLL_STEP_DOWN);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Up, 0, "scroll", "(ii)", (int)GTK_SCROLL_NONE, (int)GTK_SCROLL_STEP_UP);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Right, GDK_SHIFT_MASK, "scroll", "(ii)", (int)GTK_SCROLL_PAGE_RIGHT, (int)GTK_SCROLL_NONE);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Left, GDK_SHIFT_MASK, "scroll", "(ii)", (int)GTK_SCROLL_PAGE_LEFT, (int)GTK_SCROLL_NONE);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Up, GDK_SHIFT_MASK, "scroll", "(ii)", (int)GTK_SCROLL_NONE, (int)GTK_SCROLL_PAGE_UP);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Down, GDK_SHIFT_MASK, "scroll", "(ii)", (int)GTK_SCROLL_NONE, (int)GTK_SCROLL_PAGE_DOWN);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Page_Up, 0, "scroll", "(ii)", (int)GTK_SCROLL_NONE, (int)GTK_SCROLL_PAGE_UP);
    gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Page_Down, 0, "scroll", "(ii)", (int)GTK_SCROLL_NONE, (int)GTK_SCROLL_PAGE_DOWN);
}

/**
 * uni_image_view_new:
 * @returns: a new #UniImageView.
 *
 * Creates a new image view with default values.
 **/
GtkWidget *
uni_image_view_new (void)
{
    GtkWidget *view = g_object_new (UNI_TYPE_IMAGE_VIEW, NULL);
    return view;
}

/*************************************************************/
/***** Read-only properties **********************************/
/*************************************************************/
/**
 * uni_image_view_get_viewport:
 * @view: a #UniImageView
 * @rect: a #GdkRectangle to fill in with the current viewport or
 *   %NULL.
 * @returns: %TRUE if a #GdkPixbuf is shown, %FALSE otherwise.
 *
 * Fills in the rectangle with the current viewport. If pixbuf is
 * %NULL, there is no viewport, @rect is left untouched and %FALSE is
 * returned.
 *
 * The current viewport is defined as the rectangle, in zoomspace
 * coordinates as the area of the loaded pixbuf the #UniImageView is
 * currently showing.
 **/
gboolean
uni_image_view_get_viewport (UniImageView * view, GdkRectangle * rect)
{
    gboolean ret_val = (view->pixbuf != NULL);
    if (!rect || !ret_val)
        return ret_val;

    Size alloc = uni_image_view_get_allocated_size (view);
    Size zoomed = uni_image_view_get_zoomed_size (view);
    rect->x = view->offset_x;
    rect->y = view->offset_y;
    rect->width = MIN (alloc.width, zoomed.width);
    rect->height = MIN (alloc.height, zoomed.height);
    return TRUE;
}

/**
 * uni_image_view_get_draw_rect:
 * @view: a #UniImageView
 * @rect: a #GdkRectangle to fill in with the area of the widget in
 *   which the pixbuf is drawn.
 * @returns: %TRUE if the view is allocated and has a pixbuf, %FALSE
 *   otherwise.
 *
 * Get the rectangle in the widget where the pixbuf is painted.
 *
 * For example, if the widgets allocated size is 100, 100 and the
 * pixbufs size is 50, 50 and the zoom factor is 1.0, then the pixbuf
 * will be drawn centered on the widget. @rect will then be
 * (25,25)-[50,50].
 *
 * This method is useful when converting from widget to image or zoom
 * space coordinates.
 **/
gboolean
uni_image_view_get_draw_rect (UniImageView * view, GdkRectangle * rect)
{
    if (!view->pixbuf)
        return FALSE;
    Size alloc = uni_image_view_get_allocated_size (view);
    Size zoomed = uni_image_view_get_zoomed_size (view);

    rect->x = (alloc.width - zoomed.width) / 2;
    rect->y = (alloc.height - zoomed.height) / 2;
    rect->x = MAX (rect->x, 0);
    rect->y = MAX (rect->y, 0);
    rect->width = MIN (zoomed.width, alloc.width);
    rect->height = MIN (zoomed.height, alloc.height);
    return TRUE;
}

/*************************************************************/
/***** Write-only properties *********************************/
/*************************************************************/
/**
 * uni_image_view_set_offset:
 * @view: A #UniImageView.
 * @x: X-component of the offset in zoom space coordinates.
 * @y: Y-component of the offset in zoom space coordinates.
 * @invalidate: whether to invalidate the view or redraw immediately.
 *
 * Sets the offset of where in the image the #UniImageView should
 * begin displaying image data.
 *
 * The offset is clamped so that it will never cause the #UniImageView
 * to display pixels outside the pixbuf. Setting this attribute causes
 * the widget to repaint itself if it is realized.
 *
 * If @invalidate is %TRUE, the views entire area will be invalidated
 * instead of redrawn immediately. The view is then queued for redraw,
 * which means that additional operations can be performed on it
 * before it is redrawn.
 *
 * The difference can sometimes be important like when you are
 * overlaying data and get flicker or artifacts when setting the
 * offset. If that happens, setting @invalidate to %TRUE could fix the
 * problem. See the source code to #GtkImageToolSelector for an
 * example.
 *
 * Normally, @invalidate should always be %FALSE because it is much
 * faster to repaint immedately than invalidating.
 **/
void
uni_image_view_set_offset (UniImageView * view,
                           gdouble offset_x,
                           gdouble offset_y, gboolean invalidate)
{
    uni_image_view_scroll_to (view, offset_x, offset_y, TRUE, invalidate);
}

/*************************************************************/
/***** Read-write properties *********************************/
/*************************************************************/
void
uni_image_view_set_fitting (UniImageView * view, UniFittingMode fitting)
{
    g_return_if_fail (UNI_IS_IMAGE_VIEW (view));
    view->fitting = fitting;
    gtk_widget_queue_resize (GTK_WIDGET (view));
}

void
uni_image_view_set_vadjustment(UniImageView * view, GtkAdjustment *vadj)
{
    uni_image_view_set_scroll_adjustments(view, NULL, vadj);
}

void
uni_image_view_set_hadjustment(UniImageView * view, GtkAdjustment *hadj)
{
    uni_image_view_set_scroll_adjustments(view, hadj, NULL);
}

GtkAdjustment *
uni_image_view_get_vadjustment(UniImageView * view)
{
  return view->priv->vadjustment;
}

GtkAdjustment *
uni_image_view_get_hadjustment(UniImageView * view)
{
  return view->priv->hadjustment;
}

/**
 * uni_image_view_get_pixbuf:
 * @view: A #UniImageView.
 * @returns: The pixbuf this view shows.
 *
 * Returns the pixbuf this view shows.
 **/
GdkPixbuf *
uni_image_view_get_pixbuf (UniImageView * view)
{
    g_return_val_if_fail (UNI_IS_IMAGE_VIEW (view), NULL);
    return view->pixbuf;
}

/**
 * uni_image_view_set_pixbuf:
 * @view: A #UniImageView.
 * @pixbuf: The pixbuf to display.
 * @reset_fit: Whether to reset fitting or not.
 *
 * Sets the @pixbuf to display, or %NULL to not display any pixbuf.
 * Normally, @reset_fit should be %TRUE which enables fitting. Which
 * means that, initially, the whole pixbuf will be shown.
 *
 * Sometimes, the fit mode should not be reset. For example, if
 * UniImageView is showing an animation, it would be bad to reset the
 * fit mode for each new frame. The parameter should then be %FALSE
 * which leaves the fit mode of the view untouched.
 *
 * This method should not be used if merely the contents of the pixbuf
 * has changed. See uni_image_view_damage_pixels() for that.
 *
 * If @reset_fit is %TRUE, the ::zoom-changed signal is emitted,
 * otherwise not. The ::pixbuf-changed signal is also emitted.
 *
 * The default pixbuf is %NULL.
 **/
void
uni_image_view_set_pixbuf (UniImageView * view,
                           GdkPixbuf * pixbuf, gboolean reset_fit)
{
    if (view->pixbuf != pixbuf)
    {
        if (view->pixbuf)
            g_object_unref (view->pixbuf);
        view->pixbuf = pixbuf;
        if (view->pixbuf)
            g_object_ref (pixbuf);
    }

    if (reset_fit)
        uni_image_view_set_fitting (view, UNI_FITTING_NORMAL);
    else
    {
        /*
           If the size of the pixbuf changes, the offset might point to
           pixels outside it so we use uni_image_view_scroll_to() to
           make it valid again. And if the size is different, naturally
           we must also update the adjustments.
         */
        uni_image_view_scroll_to (view, view->offset_x, view->offset_y,
                                  FALSE, FALSE);
        uni_image_view_update_adjustments (view);
        gtk_widget_queue_draw (GTK_WIDGET (view));
    }

    g_signal_emit (G_OBJECT (view),
                   uni_image_view_signals[PIXBUF_CHANGED], 0);
    uni_dragger_pixbuf_changed (UNI_DRAGGER(view->tool), reset_fit, NULL);
}

/**
 * uni_image_view_set_zoom:
 * @view: a #UniImageView
 * @zoom: the new zoom factor
 *
 * Sets the zoom of the view.
 *
 * Fitting is always disabled after this method has run. The
 * ::zoom-changed signal is unconditionally emitted.
 **/
void
uni_image_view_set_zoom (UniImageView * view, gdouble zoom)
{
    g_return_if_fail (UNI_IS_IMAGE_VIEW (view));
    zoom = CLAMP (zoom, UNI_ZOOM_MIN, UNI_ZOOM_MAX);
    uni_image_view_set_zoom_no_center (view, zoom, FALSE);
}


void
uni_image_view_set_zoom_mode (UniImageView * view, VnrPrefsZoom mode)
{
    switch(mode)
    {
        case VNR_PREFS_ZOOM_NORMAL:
            uni_image_view_set_fitting(view, UNI_FITTING_NONE);
            //view->zoom = 1.0;
            uni_image_view_set_zoom (view, 1.0);
        break;
        case VNR_PREFS_ZOOM_FIT:
            uni_image_view_set_fitting(view, UNI_FITTING_FULL);
        break;
        case VNR_PREFS_ZOOM_SMART:
            uni_image_view_set_fitting(view, UNI_FITTING_NORMAL);
        break;
        default: break;
    }
}

/*************************************************************/
/***** Actions ***********************************************/
/*************************************************************/
/**
 * uni_image_view_zoom_in:
 * @view: a #UniImageView
 *
 * Zoom in the view one step. Calling this method causes the widget to
 * immediately repaint itself.
 **/
void
uni_image_view_zoom_in (UniImageView * view)
{
    gdouble zoom;
    zoom = CLAMP (view->zoom * UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX);
    uni_image_view_set_zoom (view, zoom);
}

/**
 * uni_image_view_zoom_out:
 * @view: a #UniImageView
 *
 * Zoom out the view one step. Calling this method causes the widget to
 * immediately repaint itself.
 **/
void
uni_image_view_zoom_out (UniImageView * view)
{
    gdouble zoom;
    zoom = CLAMP (view->zoom / UNI_ZOOM_STEP, UNI_ZOOM_MIN, UNI_ZOOM_MAX);
    uni_image_view_set_zoom (view, zoom);
}
