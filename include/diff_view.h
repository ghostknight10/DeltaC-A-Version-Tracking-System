#ifndef DIFF_VIEW_H
#define DIFF_VIEW_H

#include <gtk/gtk.h>

void create_diff_window(GtkWindow* parent, const char* file1_path, const char* file2_path, GtkListBoxRow* version_row);

#endif // DIFF_VIEW_H