#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <gtk/gtk.h>

/**
 * Creates the sidebar widget.
 *
 * This function builds a vertical box containing:
 * 1. A browse button at the top.
 * 2. A GtkListBox below it for file names.
 *
 * @param parent_window The main GtkWindow, needed to parent the file chooser dialog.
 * @return A GtkWidget pointer to the fully constructed sidebar (a GtkBox).
 */
GtkWidget *create_sidebar(GtkWindow *parent_window);

/* Clear all children from a container (list box) */
void clear_list_box_widget(GtkWidget *box_widget);

/* Populate versions list for an original file path */
void populate_versions_for_path(GtkWindow *parent, GtkListBox *versions_list, const char *original_path);

#endif // SIDEBAR_H
