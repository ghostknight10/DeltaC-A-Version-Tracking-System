#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include <gtk/gtk.h>

/**
 * @brief A signal handler for a GtkGestureClick "pressed" signal.
 * * This function is intended to be connected to a GtkGestureClick
 * controller that is set to GDK_BUTTON_SECONDARY (right-click).
 * * It creates, configures, and displays a GtkPopoverMenu at the
 * location of the click. It also handles adding the necessary
 * GActions to the widget's hierarchy.
 *
 * @param gesture The GtkGestureClick controller that emitted the signal.
 * @param n_press The number of presses (we don't use this but it's required).
 * @param x       The x-coordinate of the click (relative to the widget).
 * @param y       The y-coordinate of the click (relative to the widget).
 * @param user_data Custom data (we are not using it here, so it can be NULL).
 */
void on_widget_right_click(GtkGestureClick *gesture,
                           int n_press,
                           double x,
                           double y,
                           gpointer user_data);

#endif // CONTEXT_MENU_H