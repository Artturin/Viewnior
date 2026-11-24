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
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
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
    g_signal_emit_by_name (view->priv->hadjustment, "changed");
    g_signal_emit_by_name (view->priv->vadjustment, "changed");
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

/* The zoom one step in or out from the current one */
static gdouble
uni_image_view_step_zoom (UniImageView * view, gboolean in)
{
    gdouble zoom = in ? view->zoom * UNI_ZOOM_STEP : view->zoom / UNI_ZOOM_STEP;
    return CLAMP (zoom, UNI_ZOOM_MIN, UNI_ZOOM_MAX);
}

/* Zoom one step in, keeping the point (@x, @y) of the widget in place */
static void
uni_image_view_zoom_in_at (UniImageView * view, gdouble x, gdouble y)
{
    uni_image_view_set_zoom_with_center (view, uni_image_view_step_zoom (view, TRUE),
                                         x, y, FALSE);
}

/* Zoom one step out, keeping the point (@x, @y) of the widget in place */
static void
uni_image_view_zoom_out_at (UniImageView * view, gdouble x, gdouble y)
{
    uni_image_view_set_zoom_with_center (view, uni_image_view_step_zoom (view, FALSE),
                                         x, y, FALSE);
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
    VnrWindow *vnr_win = VNR_WINDOW(gtk_widget_get_toplevel (GTK_WIDGET (view)));
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

    GtkStyleContext *context = gtk_widget_get_style_context (widget);

    GdkRectangle borders[4];
    GdkRectangle outer = { 0, 0, alloc.width, alloc.height };
    uni_rectangle_get_rects_around (&outer, image_area, borders);
    for (n = 0; n < 4; n++)
    {
        if (borders[n].width <= 0 || borders[n].height <= 0)
            continue;
        gtk_render_background (context, cr,
                               borders[n].x, borders[n].y,
                               borders[n].width, borders[n].height);
    }
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

/**
 * uni_image_view_scroll_to:
 * @offset_x: X part of the offset in zoom space coordinates.
 * @offset_y: Y part of the offset in zoom space coordinates.
 * @set_adjustments: whether to update the adjustments. Because this
 *   function is called from the adjustments callbacks, it needs to be
 *   %FALSE to prevent infinite recursion.
 * @invalidate: whether to invalidate the view or redraw immedately,
 *  see uni_image_view_set_offset()
 *
 * Set the offset of where in the image the #UniImageView should begin
 * to display image data.
 **/
static void
uni_image_view_scroll_to (UniImageView * view,
                          gdouble offset_x,
                          gdouble offset_y,
                          gboolean set_adjustments, gboolean invalidate)
{
    GdkWindow *window;
    int delta_x, delta_y;

    uni_image_view_clamp_offset (view, &offset_x, &offset_y);

    /* Round avoids floating point to integer conversion errors. See
     */
    delta_x = floor (offset_x - view->offset_x + 0.5);
    delta_y = floor (offset_y - view->offset_y + 0.5);

    /* Exit early if the scroll was smaller than one (zoom space)
       pixel. */
    if (delta_x == 0 && delta_y == 0)
        return;

    view->offset_x = offset_x;
    view->offset_y = offset_y;

    window = gtk_widget_get_window (GTK_WIDGET (view));
    if (set_adjustments)
    {
        g_signal_handlers_block_by_data (G_OBJECT (view->priv->hadjustment), view);
        g_signal_handlers_block_by_data (G_OBJECT (view->priv->vadjustment), view);
        gtk_adjustment_set_value (view->priv->hadjustment, view->offset_x);
        gtk_adjustment_set_value (view->priv->vadjustment, view->offset_y);
        g_signal_handlers_unblock_by_data (G_OBJECT (view->priv->hadjustment), view);
        g_signal_handlers_unblock_by_data (G_OBJECT (view->priv->vadjustment), view);
    }

    if (window)
    {
        if (invalidate)
            gdk_window_invalidate_rect (window, NULL, TRUE);
        else
            /* In GTK 3.24 this does not copy the old contents; it
               invalidates the whole window, so the next draw repaints
               everything. */
            gdk_window_scroll (window, -delta_x, -delta_y);
    }
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
uni_image_view_realize (GtkWidget * widget)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    gtk_widget_set_realized (widget, TRUE);

    GtkAllocation allocation;
    gtk_widget_get_allocation (widget, &allocation);

    GdkWindowAttr attrs;
    attrs.window_type = GDK_WINDOW_CHILD;
    attrs.x = allocation.x;
    attrs.y = allocation.y;
    attrs.width = allocation.width;
    attrs.height = allocation.height;
    attrs.wclass = GDK_INPUT_OUTPUT;
    attrs.visual = gtk_widget_get_visual (widget);
    attrs.event_mask = (gtk_widget_get_events (widget)
                        | GDK_SCROLL_MASK
                        | GDK_EXPOSURE_MASK
                        | GDK_BUTTON_MOTION_MASK
                        | GDK_BUTTON_PRESS_MASK
                        | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    int attr_mask = (GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
    GdkWindow *parent = gtk_widget_get_parent_window (widget);

    GdkWindow *window = gdk_window_new (parent, &attrs, attr_mask);
    gtk_widget_set_window (widget, window);
    gdk_window_set_user_data (window, view);

    view->void_cursor = gdk_cursor_new_for_display (gtk_widget_get_display (widget), GDK_ARROW);
}

static void
uni_image_view_unrealize (GtkWidget * widget)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    g_clear_object (&view->void_cursor);
    GTK_WIDGET_CLASS (uni_image_view_parent_class)->unrealize (widget);
}

static void
uni_image_view_size_allocate (GtkWidget * widget, GtkAllocation * alloc)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    if (gtk_widget_get_realized (widget))
    {
        gtk_widget_set_allocation (widget, alloc);
    }
    else
    {
        GtkWidget *window = VNR_WINDOW (gtk_widget_get_toplevel (widget))->scroll_view;
        GtkAllocation allocation;
        gtk_widget_get_allocation (window, &allocation);
        gtk_widget_set_allocation (widget, &allocation);
    }

    if (view->pixbuf && view->fitting != UNI_FITTING_NONE)
        uni_image_view_zoom_to_fit (view, TRUE);

    uni_image_view_clamp_offset (view, &view->offset_x, &view->offset_y);

    uni_image_view_update_adjustments (view);

    if (gtk_widget_get_realized (widget))
        gdk_window_move_resize (gtk_widget_get_window (widget),
                                alloc->x, alloc->y,
                                alloc->width, alloc->height);
}

static int
uni_image_view_expose (GtkWidget * widget, cairo_t *cr)
{
    GtkAllocation allocation;
    gtk_widget_get_allocation (GTK_WIDGET (VNR_WINDOW (gtk_widget_get_toplevel (widget))->scroll_view), &allocation);
    allocation.x = 0;
    allocation.y = 0;

    /* Only repaint the damaged part. */
    GdkRectangle clip;
    if (gdk_cairo_get_clip_rectangle (cr, &clip))
        gdk_rectangle_intersect (&allocation, &clip, &allocation);

    return uni_image_view_repaint_area (UNI_IMAGE_VIEW (widget), &allocation, cr);
}

/* Press-drag-release in the context menu on Wayland.
 *
 * The menu pops up on button press. Until the button is released the
 * compositor keeps sending pointer events to the surface the press
 * happened on, so GTK hands the menu motion and release events whose
 * window belongs to the image view. GtkMenu only reacts to events on
 * its own items and ignores these, so holding the button and moving
 * over the menu highlights nothing, and releasing activates nothing.
 * On X11 the menu's pointer grab redirects the events to the menu, which
 * is why the gesture works there.
 *
 * Map such events onto the menu here instead. GDK places a Wayland
 * toplevel and the popups positioned relative to it in one shared root
 * coordinate space, so an event's root coordinates can be compared with
 * the menu items' origins. The origins GDK reports for a popup are off,
 * though, so each menu's error is worked out from where the compositor
 * really placed it (see popup_menu_item_at()). */

/* Mirrors MENU_SHELL_TIMEOUT in GTK: a release this soon after the
   press that opened the menu counts as a click, which leaves the menu
   open. */
#define POPUP_MENU_CLICK_TIMEOUT 500

/* The size of the shadow GTK draws around @menu's popup window, which
   lies outside the window geometry the compositor positions. GtkWindow
   allocates the menu just inside the shadow, offset further only by
   its own border width and the menu's CSS margin. */
static void
popup_menu_shadow (GtkMenu *menu, int *left, int *top)
{
    GtkWidget *widget = GTK_WIDGET (menu);
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
    int border = gtk_container_get_border_width (GTK_CONTAINER (toplevel));

    GtkBorder margin;
    gtk_style_context_get_margin (context, gtk_style_context_get_state (context),
                                  &margin);

    GtkAllocation alloc;
    gtk_widget_get_allocation (widget, &alloc);

    *left = alloc.x - border - margin.left;
    *top = alloc.y - border - margin.top;
}

/* Where the compositor placed @menu's window geometry, relative to the
   window it was positioned against. */
static void
popup_menu_popped_up_cb (GtkMenu *menu, const GdkRectangle *flipped_rect,
                         const GdkRectangle *final_rect, gboolean flipped_x,
                         gboolean flipped_y, gpointer data)
{
    g_object_set_data (G_OBJECT (menu), "vnr-popup-placed", GINT_TO_POINTER (TRUE));
    g_object_set_data (G_OBJECT (menu), "vnr-popup-x", GINT_TO_POINTER (final_rect->x));
    g_object_set_data (G_OBJECT (menu), "vnr-popup-y", GINT_TO_POINTER (final_rect->y));
}

/* Finds the item of @menu, or of its open submenus, at the given true
   root coordinates.

   @rect_window is the window GTK positioned @menu against: the image
   view for the context menu, the parent menu's item for a submenu.
   @parent_error is how far the root origins GDK reports for
   @rect_window are off.

   GDK places a popup at its position relative to @rect_window, but
   adds that to the origin of @rect_window's native parent window
   rather than to @rect_window's own origin. It also leaves out the
   popup's shadow. The popup's real position is known, though, as
   popped-up reports where its window geometry was placed relative to
   @rect_window. */
static GtkWidget *
popup_menu_item_at (GtkMenu *menu, GdkWindow *rect_window,
                    int parent_error_x, int parent_error_y,
                    gdouble x_root, gdouble y_root)
{
    if (!gtk_widget_get_mapped (GTK_WIDGET (menu)) ||
        g_object_get_data (G_OBJECT (menu), "vnr-popup-placed") == NULL)
        return NULL;

    int shadow_left, shadow_top;
    popup_menu_shadow (menu, &shadow_left, &shadow_top);

    int rect_x, rect_y;
    gdk_window_get_origin (rect_window, &rect_x, &rect_y);
    int real_x = rect_x - parent_error_x - shadow_left +
                 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu), "vnr-popup-x"));
    int real_y = rect_y - parent_error_y - shadow_top +
                 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu), "vnr-popup-y"));

    int reported_x, reported_y;
    gdk_window_get_origin (gtk_widget_get_window (gtk_widget_get_toplevel (GTK_WIDGET (menu))),
                           &reported_x, &reported_y);
    int error_x = reported_x - real_x;
    int error_y = reported_y - real_y;

    /* An open submenu is stacked above its parent, so look there first. */
    GtkWidget *selected = gtk_menu_shell_get_selected_item (GTK_MENU_SHELL (menu));
    if (selected != NULL)
    {
        GtkWidget *submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (selected));
        if (submenu != NULL)
        {
            GtkWidget *item = popup_menu_item_at (GTK_MENU (submenu),
                                                  gtk_widget_get_window (selected),
                                                  error_x, error_y,
                                                  x_root, y_root);
            if (item != NULL)
                return item;
        }
    }

    /* Compare against the origins GDK reports for this menu from here. */
    x_root += error_x;
    y_root += error_y;

    /* Ignore items scrolled out of the visible part of the menu. */
    GdkWindow *window = gtk_widget_get_window (GTK_WIDGET (menu));
    int x, y;
    gdk_window_get_origin (window, &x, &y);
    if (x_root < x || x_root >= x + gdk_window_get_width (window) ||
        y_root < y || y_root >= y + gdk_window_get_height (window))
        return NULL;

    GtkWidget *found = NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (menu));
    for (GList *l = children; l != NULL && found == NULL; l = l->next)
    {
        GtkWidget *item = l->data;
        if (!gtk_widget_get_mapped (item))
            continue;

        /* Menu items draw on the menu's scrolled bin window, which
           their allocation is relative to. */
        GtkAllocation alloc;
        gtk_widget_get_allocation (item, &alloc);
        gdk_window_get_origin (gtk_widget_get_window (item), &x, &y);
        x += alloc.x;
        y += alloc.y;
        if (x_root >= x && x_root < x + alloc.width &&
            y_root >= y && y_root < y + alloc.height)
            found = item;
    }
    g_list_free (children);
    return found;
}

/* Finds the item of the context menu @root, or of its open submenus,
   at the root coordinates of an event on the main window. */
static GtkWidget *
popup_menu_item_at_pointer (GtkMenu *root, gdouble x_root, gdouble y_root)
{
    GdkWindow *rect_window = g_object_get_data (G_OBJECT (root), "vnr-rect-window");
    if (rect_window == NULL)
        return NULL;
    return popup_menu_item_at (root, rect_window, 0, 0, x_root, y_root);
}

static void
popup_menu_unmap_cb (GtkWidget *menu, gpointer data)
{
    g_object_set_data (G_OBJECT (menu), "vnr-popup-placed", NULL);
}

/* Whether @event is on the main window, which the context menu @root
   was popped up from, rather than on one of the menus. GtkMenu handles
   events on the menus itself, and their root coordinates are relative
   to the origins GDK reports for the menu, not the true ones
   popup_menu_item_at() expects. */
static gboolean
popup_menu_event_on_view (GdkEvent *event, GtkMenu *root)
{
    GdkWindow *rect_window = g_object_get_data (G_OBJECT (root), "vnr-rect-window");
    return rect_window != NULL &&
           gdk_window_get_toplevel (event->any.window) ==
           gdk_window_get_toplevel (rect_window);
}

/* @menu is the innermost open menu, which holds the GTK grab and so
   receives the events; @root is the context menu itself. */
static gboolean
popup_menu_motion_cb (GtkWidget *menu, GdkEventMotion *ev, GtkMenu *root)
{
    if (!popup_menu_event_on_view ((GdkEvent *) ev, root))
        return FALSE;

    GtkWidget *item = popup_menu_item_at_pointer (root, ev->x_root, ev->y_root);
    if (item != NULL)
    {
        /* Selecting an item also pops its submenu up, or the submenu
           of the previously selected item down. */
        GtkMenuShell *shell = GTK_MENU_SHELL (gtk_widget_get_parent (item));
        if (gtk_menu_shell_get_selected_item (shell) != item)
            gtk_menu_shell_select_item (shell, item);
    }
    else
    {
        /* Like leaving an item: drop the highlight, unless the item
           has a submenu the pointer may be heading to. */
        GtkMenuShell *shell = GTK_MENU_SHELL (menu);
        GtkWidget *selected = gtk_menu_shell_get_selected_item (shell);
        if (selected != NULL &&
            gtk_menu_item_get_submenu (GTK_MENU_ITEM (selected)) == NULL)
            gtk_menu_shell_deselect (shell);
    }

    /* GtkMenu still gets the event, to scroll a menu that does not fit
       while the pointer is over a scroll arrow. It compares the root
       coordinates with the origin GDK reports for the menu, though, so
       during the drag the arrows react off from where they are drawn,
       by the same error popup_menu_item_at() corrects for. */
    return FALSE;
}

static gboolean
popup_menu_release_cb (GtkWidget *menu, GdkEventButton *ev, GtkMenu *root)
{
    if (!popup_menu_event_on_view ((GdkEvent *) ev, root))
        return FALSE;

    guint32 popup_time =
        GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (root), "vnr-popup-time"));
    if (ev->time - popup_time < POPUP_MENU_CLICK_TIMEOUT)
        return FALSE;

    /* Off the items GtkMenu closes the menu, as it does for a release
       outside of it. */
    GtkWidget *item = popup_menu_item_at_pointer (root, ev->x_root, ev->y_root);
    if (item == NULL)
        return FALSE;

    /* Like GtkMenuShell, releasing on a separator or an insensitive item
       leaves the menu open. */
    if (GTK_IS_SEPARATOR_MENU_ITEM (item) || !gtk_widget_is_sensitive (item))
        return TRUE;

    /* Only act on the item the drag highlighted. */
    GtkMenuShell *shell = GTK_MENU_SHELL (gtk_widget_get_parent (item));
    if (gtk_menu_shell_get_selected_item (shell) != item)
        return FALSE;

    /* Releasing on an item with a submenu leaves the menu open. */
    if (gtk_menu_item_get_submenu (GTK_MENU_ITEM (item)) == NULL)
        gtk_menu_shell_activate_item (shell, item, TRUE);
    return TRUE;
}

static void
popup_menu_track_drag (GtkMenu *menu, GtkMenu *root)
{
    /* Submenus are rebuilt, like the Open With one, so check them on
       every popup. */
    if (g_object_get_data (G_OBJECT (menu), "vnr-drag-tracked") == NULL)
    {
        g_object_set_data (G_OBJECT (menu), "vnr-drag-tracked", GINT_TO_POINTER (TRUE));
        g_signal_connect (menu, "motion-notify-event",
                          G_CALLBACK (popup_menu_motion_cb), root);
        g_signal_connect (menu, "button-release-event",
                          G_CALLBACK (popup_menu_release_cb), root);
        g_signal_connect (menu, "popped-up",
                          G_CALLBACK (popup_menu_popped_up_cb), NULL);
        g_signal_connect (menu, "unmap",
                          G_CALLBACK (popup_menu_unmap_cb), NULL);
    }

    GList *children = gtk_container_get_children (GTK_CONTAINER (menu));
    for (GList *l = children; l != NULL; l = l->next)
    {
        if (!GTK_IS_MENU_ITEM (l->data))
            continue;
        GtkWidget *submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (l->data));
        if (submenu != NULL)
            popup_menu_track_drag (GTK_MENU (submenu), root);
    }
    g_list_free (children);
}

static void
uni_image_view_popup_menu (GtkWidget *widget, GdkEventButton *ev)
{
    GtkMenu *menu = GTK_MENU (VNR_WINDOW (gtk_widget_get_toplevel (widget))->popup_menu);

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (widget)))
    {
        g_object_set_data (G_OBJECT (menu), "vnr-popup-time",
                           GUINT_TO_POINTER (ev->time));
        /* gtk_menu_popup_at_pointer() positions the menu against the
           trigger event's window. */
        g_object_set_data (G_OBJECT (menu), "vnr-rect-window", ev->window);
        popup_menu_track_drag (menu, menu);
    }
#endif

    gtk_menu_popup_at_pointer (menu, (GdkEvent *) ev);
}

static int
uni_image_view_button_press (GtkWidget * widget, GdkEventButton * ev)
{
    gtk_widget_grab_focus(widget);
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    VnrWindow *vnr_win = VNR_WINDOW(gtk_widget_get_toplevel(widget));
    g_assert(gtk_widget_is_toplevel(GTK_WIDGET(vnr_win)));

    if(ev->type == GDK_2BUTTON_PRESS && ev->button == 1 && vnr_win->prefs->behavior_click == VNR_PREFS_CLICK_FULLSCREEN)
    {
        vnr_window_toggle_fullscreen(vnr_win);
        return 1;
    }
    else if(ev->type == GDK_2BUTTON_PRESS && ev->button == 1 && vnr_win->prefs->behavior_click == VNR_PREFS_CLICK_NEXT)
    {
        int width = gdk_window_get_width(gtk_widget_get_window(widget));

        if(ev->x/width < 0.5)
            vnr_window_prev(vnr_win);
        else
            vnr_window_next(vnr_win, TRUE);

        return 1;
    }
    else if (ev->type == GDK_BUTTON_PRESS && ev->button == 1)
    {
        return uni_dragger_button_press (UNI_DRAGGER(view->tool), ev);
    }
    else if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1)
    {
        if (view->fitting == UNI_FITTING_FULL ||
            (view->fitting == UNI_FITTING_NORMAL && view->zoom != 1.0))
            uni_image_view_set_zoom_with_center (view, 1., ev->x, ev->y,
                                                 FALSE);
        else
            uni_image_view_set_fitting (view, UNI_FITTING_FULL);
        return 1;
    }
    else if(ev->type == GDK_BUTTON_PRESS && ev->button == 3)
    {
        uni_image_view_popup_menu (widget, ev);
    }
    else if(ev->type == GDK_BUTTON_PRESS && ev->button == 8)
    {
        vnr_window_prev(vnr_win);
    }
    else if(ev->type == GDK_BUTTON_PRESS && ev->button == 9)
    {
        vnr_window_next(vnr_win, TRUE);
    }
    return 0;
}

static int
uni_image_view_button_release (GtkWidget * widget, GdkEventButton * ev)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    return uni_dragger_button_release (UNI_DRAGGER(view->tool), ev);
}

static int
uni_image_view_motion_notify (GtkWidget * widget, GdkEventMotion * ev)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    if (view->is_rendering)
        return FALSE;
    return uni_dragger_motion_notify (UNI_DRAGGER(view->tool), ev);
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

static int
uni_image_view_scroll_event (GtkWidget * widget, GdkEventScroll * ev)
{
    UniImageView *view = UNI_IMAGE_VIEW (widget);
    VnrWindow *vnr_win = VNR_WINDOW(gtk_widget_get_toplevel(widget));
    g_assert(gtk_widget_is_toplevel(GTK_WIDGET(vnr_win)));

    /* Horizontal scroll left is equivalent to scroll up and right is
     * like scroll down. No idea if that is correct -- I have no input
     * device that can do horizontal scrolls. */

	if (vnr_win->prefs->behavior_wheel == VNR_PREFS_WHEEL_ZOOM || (ev->state & GDK_CONTROL_MASK) != 0)
	{
        switch (ev->direction)
        {
            case GDK_SCROLL_LEFT:
                // In Zoom mode left/right scroll is used for navigation
                vnr_window_prev(vnr_win);
                break;
            case GDK_SCROLL_RIGHT:
                vnr_window_next(vnr_win, TRUE);
                break;
            case GDK_SCROLL_UP:
                if( ev->state & GDK_SHIFT_MASK ) {
                    vnr_window_prev(vnr_win);
                } else {
                    uni_image_view_zoom_in_at (view, ev->x, ev->y);
                }
                break;
            default:
                if( ev->state & GDK_SHIFT_MASK ) {
                    vnr_window_next(vnr_win, TRUE);
                } else {
                    uni_image_view_zoom_out_at (view, ev->x, ev->y);
                }
        }

	}
	else if(vnr_win->prefs->behavior_wheel == VNR_PREFS_WHEEL_NAVIGATE)
	{
        switch (ev->direction)
        {
            case GDK_SCROLL_LEFT:
                uni_image_view_zoom_in_at (view, ev->x, ev->y);
                break;

            case GDK_SCROLL_RIGHT:
                uni_image_view_zoom_out_at (view, ev->x, ev->y);
                break;

            case GDK_SCROLL_UP:
                if( ev->state & GDK_SHIFT_MASK )
                {
                    uni_image_view_zoom_in_at (view, ev->x, ev->y);
                }
                else
                    vnr_window_prev(vnr_win);

                break;

            default:
                if( ev->state & GDK_SHIFT_MASK )
                {
                    uni_image_view_zoom_out_at (view, ev->x, ev->y);
                }
                else
                    vnr_window_next(vnr_win, TRUE);
        }
	}
	else
	{
		switch (ev->direction)
		{
			case GDK_SCROLL_LEFT:
                uni_image_view_scroll (view, GTK_SCROLL_PAGE_LEFT, GTK_SCROLL_NONE);
                break;
			case GDK_SCROLL_RIGHT:
                uni_image_view_scroll (view, GTK_SCROLL_PAGE_RIGHT, GTK_SCROLL_NONE);
                break;
            case GDK_SCROLL_UP:
                if( ev->state & GDK_SHIFT_MASK )
                    uni_image_view_scroll (view, GTK_SCROLL_PAGE_LEFT, GTK_SCROLL_NONE);
                else
                    uni_image_view_scroll (view, GTK_SCROLL_NONE, GTK_SCROLL_PAGE_UP);
                break;
            default:
                if( ev->state & GDK_SHIFT_MASK )
                    uni_image_view_scroll (view, GTK_SCROLL_PAGE_RIGHT, GTK_SCROLL_NONE);
                else
                    uni_image_view_scroll (view, GTK_SCROLL_NONE, GTK_SCROLL_PAGE_DOWN);
		}
	}

    return TRUE;
}

/* Replace the adjustment in @slot with @adj, or with nothing if @adj is
 * NULL, and call @value_changed when the value of @adj changes. */
static void
uni_image_view_replace_adjustment (UniImageView * view,
                                   GtkAdjustment ** slot,
                                   GtkAdjustment * adj,
                                   GCallback value_changed)
{
    if (*slot == adj)
        return;

    if (*slot)
    {
        g_signal_handlers_disconnect_by_data (G_OBJECT (*slot), view);
        g_object_unref (*slot);
    }

    *slot = adj;

    if (adj)
    {
        g_signal_connect (adj, "value_changed", value_changed, view);
        g_object_ref_sink (adj);
    }
}

static void
uni_image_view_set_scroll_adjustments (UniImageView * view,
                                       GtkAdjustment * hadj,
                                       GtkAdjustment * vadj)
{
    if (hadj)
        uni_image_view_replace_adjustment (view, &view->priv->hadjustment, hadj,
                                           G_CALLBACK (uni_image_view_hadj_changed_cb));
    if (vadj)
        uni_image_view_replace_adjustment (view, &view->priv->vadjustment, vadj,
                                           G_CALLBACK (uni_image_view_vadj_changed_cb));
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
    view->void_cursor = NULL;
    view->tool = G_OBJECT (uni_dragger_new ((GtkWidget *) view));

    view->priv = uni_image_view_get_instance_private (view);

    view->priv->hadjustment = view->priv->vadjustment = NULL;
    uni_image_view_set_scroll_adjustments(view, GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 1.0, 0.0,
                                          1.0, 1.0, 1.0)), GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 1.0, 0.0,
                                          1.0, 1.0, 1.0)));
    g_object_ref_sink (view->priv->hadjustment);
    g_object_ref_sink (view->priv->vadjustment);
}

static void
uni_image_view_finalize (GObject * object)
{
    UniImageView *view = UNI_IMAGE_VIEW (object);
    uni_image_view_replace_adjustment (view, &view->priv->hadjustment, NULL, NULL);
    uni_image_view_replace_adjustment (view, &view->priv->vadjustment, NULL, NULL);
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
    widget_class->button_press_event = uni_image_view_button_press;
    widget_class->button_release_event = uni_image_view_button_release;
    widget_class->draw = uni_image_view_expose;
    widget_class->motion_notify_event = uni_image_view_motion_notify;
    widget_class->realize = uni_image_view_realize;
    widget_class->scroll_event = uni_image_view_scroll_event;
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
    GtkBindingSet *binding_set = gtk_binding_set_by_class (klass);

    /* Set zoom. */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_1, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 1.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_2, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 2.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_3, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 3.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_1, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 1.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_2, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 2.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_3, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 3.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_5, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 5.0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_5, 0,
                                  "set_zoom", 1, G_TYPE_DOUBLE, 5.0);

    /* Zoom in */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_plus, 0, "zoom_in", 0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_equal, 0, "zoom_in", 0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Add, 0, "zoom_in", 0);

    /* Zoom out */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_minus, 0, "zoom_out", 0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Subtract, 0,
                                  "zoom_out", 0);

    /* Set fitting */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_f, 0,
                                  "set_fitting", 1, G_TYPE_ENUM, UNI_FITTING_FULL);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_0, 0,
                                  "set_fitting", 1, G_TYPE_ENUM, UNI_FITTING_FULL);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_0, 0,
                                  "set_fitting", 1, G_TYPE_ENUM, UNI_FITTING_FULL);

    /* Unmodified scrolling */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Right, 0,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_STEP_RIGHT,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_NONE);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Left, 0,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_STEP_LEFT,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_NONE);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Down, 0,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_NONE,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_DOWN);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Up, 0,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_NONE,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_UP);

    /* Shifted scrolling */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Right, GDK_SHIFT_MASK,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_PAGE_RIGHT,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_NONE);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Left, GDK_SHIFT_MASK,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_PAGE_LEFT,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_NONE);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Up, GDK_SHIFT_MASK,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_NONE,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_PAGE_UP);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Down, GDK_SHIFT_MASK,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_NONE,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_PAGE_DOWN);

    /* Page Up & Down */
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Page_Up, 0,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_NONE,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_PAGE_UP);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Page_Down, 0,
                                  "scroll", 2,
                                  GTK_TYPE_SCROLL_TYPE,
                                  GTK_SCROLL_NONE,
                                  GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_PAGE_DOWN);

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
    uni_image_view_set_zoom (view, uni_image_view_step_zoom (view, TRUE));
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
    uni_image_view_set_zoom (view, uni_image_view_step_zoom (view, FALSE));
}
