#include "sidebar.h" // Or "temp.h" as your file includes
#include "context_menu.h"
#include <gtk/gtk.h>
#include <glib/gstdio.h> // For g_path_get_basename
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(__has_include)
# if __has_include(<json-glib/json-glib.h>)
#  include <json-glib/json-glib.h>
#  define HAVE_JSON_GLIB 1
# endif
#endif
#if defined(G_OS_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

// This struct now holds all widgets our sidebar needs
typedef struct {
    GtkWindow *parent_window;
    GtkWidget *list_box;
    GtkWidget *delete_button; // So we can enable/disable it
} SidebarData;

/* Forward: populate_versions_for_path is used externally */
void populate_versions_for_path(GtkWindow *parent, GtkListBox *versions_list, const char *original_path);

/* Clear all children from a container (list box) */
void clear_list_box_widget(GtkWidget *box_widget) {
    if (!GTK_IS_WIDGET(box_widget)) return;

    if (GTK_IS_LIST_BOX(box_widget)) {
        GtkListBox *box = GTK_LIST_BOX(box_widget);
        GtkListBoxRow *row;
        while ((row = gtk_list_box_get_row_at_index(box, 0)) != NULL) {
            gtk_list_box_remove(box, GTK_WIDGET(row));
        }
    } else {
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(box_widget)) != NULL) {
            gtk_widget_unparent(child);
        }
    }
}

/* Add a full path to the sidebar list (creates row, label, gesture, stores file-path) */
static void add_path_to_list(SidebarData *data, const char *full_path) {
    if (!data || !full_path) return;
    char *basename = g_path_get_basename(full_path);
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *list_row_label = gtk_label_new(basename);
    gtk_widget_set_halign(list_row_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(list_row_label), TRUE);
    gtk_box_append(GTK_BOX(hbox), list_row_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    gtk_widget_set_name(row, basename);
    g_object_set_data(G_OBJECT(row), "label-widget", list_row_label);
    GtkGesture *right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    /* Do not make the right-click gesture exclusive — that can prevent
     * normal left-click selection from reaching the list box. */
    gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(right_click), FALSE);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_widget_right_click), (gpointer)"sidebar-element");
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(right_click));
    g_object_set_data_full(G_OBJECT(row), "file-path", g_strdup(full_path), g_free);
    gtk_list_box_append(GTK_LIST_BOX(data->list_box), row);
    g_free(basename);
}

/* Populate versions list for an original file path */
void populate_versions_for_path(GtkWindow *parent, GtkListBox *versions_list, const char *original_path) {
    if (!versions_list) return;
    clear_list_box_widget(GTK_WIDGET(versions_list));
    const char *data_dir = "data";
    /* Try JSON index first (if available), otherwise fallback to text index */
#ifdef HAVE_JSON_GLIB
    gchar *index_path = g_build_filename(data_dir, "versions_index.json", NULL);
    if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {
        GError *error = NULL;
        JsonParser *parser = json_parser_new();
        if (!json_parser_load_from_file(parser, index_path, &error)) {
            g_printerr("Failed to parse versions index JSON: %s\n", error ? error->message : "unknown");
            g_clear_error(&error);
        } else {
            JsonNode *root = json_parser_get_root(parser);
            if (JSON_NODE_HOLDS_ARRAY(root)) {
                JsonArray *arr = json_node_get_array(root);
                guint len = json_array_get_length(arr);
                for (guint i = 0; i < len; ++i) {
                    JsonNode *elem = json_array_get_element(arr, i);
                    if (!JSON_NODE_HOLDS_OBJECT(elem)) continue;
                    JsonObject *obj = json_node_get_object(elem);
                    const char *orig = json_object_get_string_member(obj, "original");
                    const char *stored = json_object_get_string_member(obj, "stored");
                    const char *ts = json_object_get_string_member(obj, "timestamp");
                    if (!orig || !stored) continue;
                    if (g_strcmp0(orig, original_path) == 0) {
                        char timestr_human[128] = {0};
                        if (ts && strlen(ts) >= 14) {
                            struct tm tm = {0};
                            char buf2[5];
                            memcpy(buf2, ts+0, 4); buf2[4]='\0'; tm.tm_year = atoi(buf2) - 1900;
                            memcpy(buf2, ts+4, 2); buf2[2]='\0'; tm.tm_mon = atoi(buf2) - 1;
                            memcpy(buf2, ts+6, 2); buf2[2]='\0'; tm.tm_mday = atoi(buf2);
                            memcpy(buf2, ts+8, 2); buf2[2]='\0'; tm.tm_hour = atoi(buf2);
                            memcpy(buf2, ts+10,2); buf2[2]='\0'; tm.tm_min = atoi(buf2);
                            memcpy(buf2, ts+12,2); buf2[2]='\0'; tm.tm_sec = atoi(buf2);
                            strftime(timestr_human, sizeof(timestr_human), "%Y-%m-%d %H:%M:%S", &tm);
                        } else if (ts) {
                            g_strlcpy(timestr_human, ts, sizeof(timestr_human));
                        }

                        gchar *label_text = g_strdup_printf("%s  %s", stored, timestr_human);

                        GtkWidget *vrow = gtk_list_box_row_new();
                        GtkWidget *vlabel = gtk_label_new(label_text);
                        gtk_widget_set_halign(vlabel, GTK_ALIGN_START);
                        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(vrow), vlabel);

                        gchar *stored_path = g_build_filename("data", "versions", stored, NULL);
                        g_object_set_data_full(G_OBJECT(vrow), "version-path", g_strdup(stored_path), g_free);
                        g_object_set_data_full(G_OBJECT(vrow), "file-path", g_strdup(stored_path), g_free); // Set file-path for comparison
                        g_free(stored_path);

                        /* Attach right-click gesture to version row so user can open/delete the version */
                        GtkGesture *right_click = gtk_gesture_click_new();
                        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
                        gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(right_click), FALSE);
                        g_signal_connect(right_click, "pressed", G_CALLBACK(on_widget_right_click), (gpointer)"version-element");
                        gtk_widget_add_controller(vrow, GTK_EVENT_CONTROLLER(right_click));

                        gtk_list_box_append(GTK_LIST_BOX(versions_list), vrow);
                        g_free(label_text);
                    }
                }
            }
        }
        g_object_unref(parser);
        g_free(index_path);
        return;
    }
#endif

    /* Fallback: read text-based index */
    gchar *index_path_txt = g_build_filename(data_dir, "versions_index.txt", NULL);
    if (!g_file_test(index_path_txt, G_FILE_TEST_EXISTS)) { g_free(index_path_txt); return; }
    FILE *f = fopen(index_path_txt, "r");
    if (!f) { g_free(index_path_txt); return; }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '|');
        if (!p2) continue;
        *p2 = '\0';
        const char *orig = line;
        const char *stored = p1 + 1;
        const char *ts = p2 + 1;
        if (g_strcmp0(orig, original_path) == 0) {
            char timestr_human[128] = {0};
            if (strlen(ts) >= 14) {
                struct tm tm = {0};
                char buf2[5];
                memcpy(buf2, ts+0, 4); buf2[4]='\0'; tm.tm_year = atoi(buf2) - 1900;
                memcpy(buf2, ts+4, 2); buf2[2]='\0'; tm.tm_mon = atoi(buf2) - 1;
                memcpy(buf2, ts+6, 2); buf2[2]='\0'; tm.tm_mday = atoi(buf2);
                memcpy(buf2, ts+8, 2); buf2[2]='\0'; tm.tm_hour = atoi(buf2);
                memcpy(buf2, ts+10,2); buf2[2]='\0'; tm.tm_min = atoi(buf2);
                memcpy(buf2, ts+12,2); buf2[2]='\0'; tm.tm_sec = atoi(buf2);
                strftime(timestr_human, sizeof(timestr_human), "%Y-%m-%d %H:%M:%S", &tm);
            } else {
                g_strlcpy(timestr_human, ts, sizeof(timestr_human));
            }

            gchar *label_text = g_strdup_printf("%s  %s", stored, timestr_human);

            GtkWidget *vrow = gtk_list_box_row_new();
            /* Create two-column row: filename on left, timestamp on right */
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *name_label = gtk_label_new(stored);
            gtk_widget_set_halign(name_label, GTK_ALIGN_START);
            gtk_widget_set_hexpand(name_label, TRUE);
            gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);

            GtkWidget *time_label = gtk_label_new(timestr_human);
            gtk_widget_set_halign(time_label, GTK_ALIGN_END);
            gtk_widget_set_hexpand(time_label, FALSE);
            gtk_label_set_xalign(GTK_LABEL(time_label), 1.0);

            gtk_box_append(GTK_BOX(hbox), name_label);
            gtk_box_append(GTK_BOX(hbox), time_label);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(vrow), hbox);

            gchar *stored_path = g_build_filename("data", "versions", stored, NULL);
            g_object_set_data_full(G_OBJECT(vrow), "version-path", g_strdup(stored_path), g_free);
            g_object_set_data_full(G_OBJECT(vrow), "file-path", g_strdup(stored_path), g_free); // Set file-path for comparison
            /* Also store labels for potential updates */
            g_object_set_data(G_OBJECT(vrow), "version-name-label", name_label);
            g_object_set_data(G_OBJECT(vrow), "version-time-label", time_label);
            g_free(stored_path);

            /* Attach right-click gesture to version row so user can open/delete the version */
            GtkGesture *right_click = gtk_gesture_click_new();
            gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
            gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(right_click), FALSE);
            g_signal_connect(right_click, "pressed", G_CALLBACK(on_widget_right_click), (gpointer)"version-element");
            gtk_widget_add_controller(vrow, GTK_EVENT_CONTROLLER(right_click));

            gtk_list_box_append(GTK_LIST_BOX(versions_list), vrow);
            g_free(label_text);
        }
    }
    fclose(f);
    g_free(index_path_txt);
}


// --- "Add Files" FINISH callback ---
// GTK 4.10: This is the new GAsyncReadyCallback that handles the result
// from GtkFileDialog.
static void
on_browse_open_finish(GObject *source, GAsyncResult *res, gpointer user_data) {
    SidebarData *data = (SidebarData *)user_data;
    GError *error = NULL;

    // Call the _finish function to get the GFile
    // This replaces the deprecated gtk_file_chooser_get_files
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);

    if (error) {
        g_warning("File open dialog failed: %s", error->message);
        g_error_free(error);
        g_object_unref(source); // Don't forget to unref the dialog
        return;
    }

    if (file) { // User selected a file
        char *full_path = g_file_get_path(file);

        /* Add to UI */
        void add_path_to_list(SidebarData *data, const char *full_path);
        add_path_to_list(data, full_path);

        /* Persist in data/files_index.txt (create data dir if needed) */
        const char *data_dir = "data";
        g_mkdir_with_parents(data_dir, 0755);
        gchar *index_path = g_build_filename(data_dir, "files_index.txt", NULL);
        gboolean already = FALSE;
        if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {
            FILE *f = fopen(index_path, "r");
            if (f) {
                char buf[4096];
                while (fgets(buf, sizeof(buf), f)) {
                    char *nl = strchr(buf, '\n'); if (nl) *nl = '\0';
                    if (g_strcmp0(buf, full_path) == 0) { already = TRUE; break; }
                }
                fclose(f);
            }
        }
        if (!already) {
            FILE *f = fopen(index_path, "a");
            if (f) { fprintf(f, "%s\n", full_path); fclose(f); }
        }
        g_free(index_path);

        g_free(full_path);
        g_object_unref(file); // Unref the file
    }
    // else: user clicked cancel, 'file' is NULL, do nothing.

    // Unref the dialog object itself
    g_object_unref(source);
}


// --- "Add Files" click callback ---
// GTK 4.10: This function now starts the GtkFileDialog
static void on_browse_clicked(GtkButton *button, gpointer user_data) {
    SidebarData *data = (SidebarData *)user_data;
    
    // FIX: Create a GtkFileDialog instance
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File");
    
    // You could set filters here if needed, e.g.:
    // GtkFileFilter *filter = gtk_file_filter_new();
    // gtk_file_filter_add_pattern(filter, "*.txt");
    // gtk_file_dialog_set_default_filter(dialog, filter);
    // g_object_unref(filter);

    // FIX: Call the async gtk_file_dialog_open function
    // This replaces gtk_file_chooser_native_new and gtk_native_dialog_show
    gtk_file_dialog_open(
        dialog,
        data->parent_window,
        NULL, // GCancellable
        on_browse_open_finish, // The new callback
        data // user_data
    );
    
    // The 'dialog' object will be unreferenced in the callback
}


// --- Data struct for the delete confirmation callback ---
typedef struct {
    SidebarData *sidebar_data;
    GtkListBoxRow *row_to_delete;
} DeleteCallbackData;


// --- "Delete File" response callback ---
static void
on_delete_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    DeleteCallbackData *delete_data = (DeleteCallbackData *)user_data;
    
    int response = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);

    if (response == 1) { // 1 is the "Delete" button
        gtk_list_box_remove(
            GTK_LIST_BOX(delete_data->sidebar_data->list_box),
            GTK_WIDGET(delete_data->row_to_delete)
        );
    }
    g_free(delete_data);
}


// --- "Delete File" click callback ---
static void on_delete_clicked(GtkButton *button, gpointer user_data) {
    SidebarData *data = (SidebarData *)user_data;

    GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(
        GTK_LIST_BOX(data->list_box)
    );

    if (selected_row == NULL) {
        return;
    }

    DeleteCallbackData *delete_data = g_new(DeleteCallbackData, 1);
    delete_data->sidebar_data = data;
    delete_data->row_to_delete = selected_row;

    GtkAlertDialog *alert = gtk_alert_dialog_new("Do you want to delete this file?");
    const char *buttons[] = {"Cancel", "Delete", NULL};
    gtk_alert_dialog_set_buttons(alert, buttons);
    gtk_alert_dialog_set_default_button(alert, 1);
    gtk_alert_dialog_set_cancel_button(alert, 0);

    gtk_alert_dialog_choose(
        alert,
        data->parent_window,
        NULL, // GCancellable
        on_delete_response,
        delete_data
    );
    g_object_unref(alert);
}

// --- "List Selection" callback ---
static void on_row_selected(GtkListBox *list_box, GtkListBoxRow *row, gpointer user_data) {
    SidebarData *data = (SidebarData *)user_data;
    gtk_widget_set_sensitive(data->delete_button, (row != NULL));

    /* When a row is selected, populate the versions list on the right and show it. */
    GtkWidget *toplevel = NULL;
    GtkListBox *versions_list = NULL;
    if (row != NULL) {
        /* Get stored file path on the row */
        const char *path = g_object_get_data(G_OBJECT(row), "file-path");
        toplevel = gtk_widget_get_ancestor(GTK_WIDGET(row), GTK_TYPE_WINDOW);
        if (toplevel) {
            g_object_set_data(G_OBJECT(toplevel), "original-path", (gpointer)path);
            GtkWidget *v = g_object_get_data(G_OBJECT(toplevel), "versions-list");
            if (v && GTK_IS_LIST_BOX(v)) {
                versions_list = GTK_LIST_BOX(v);
                /* Ensure visible */
                gtk_widget_set_visible(GTK_WIDGET(versions_list), TRUE);
                /* Populate with versions for this path */
                populate_versions_for_path(GTK_WINDOW(toplevel), versions_list, path ? path : "");
            }
        }
    } else {
        /* No selection: hide versions list if we can find it */
        toplevel = gtk_widget_get_ancestor(GTK_WIDGET(list_box), GTK_TYPE_WINDOW);
        if (toplevel) {
            GtkWidget *v = g_object_get_data(G_OBJECT(toplevel), "versions-list");
            if (v) gtk_widget_set_visible(v, FALSE);
        }
    }
}


// --- Main create_sidebar function (Modified) ---
GtkWidget *create_sidebar(GtkWindow *parent_window) {
    GtkWidget *sidebar_vbox, *button_hbox, *browse_button, *delete_button, *scrolled_window, *list_box, *icon;

    sidebar_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    // --- Create "Add Files" button ---
    browse_button = gtk_button_new();
    GtkWidget *browse_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    icon = gtk_image_new_from_icon_name("document-open-symbolic");
    GtkWidget *browse_label = gtk_label_new("Add Files");
    gtk_box_append(GTK_BOX(browse_box), icon);
    gtk_box_append(GTK_BOX(browse_box), browse_label);
    gtk_button_set_child(GTK_BUTTON(browse_button), browse_box);
    gtk_widget_add_css_class(browse_button, "sidebar-button");

    // --- Create "Delete" button ---
    delete_button = gtk_button_new();
    GtkWidget *delete_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    icon = gtk_image_new_from_icon_name("edit-delete-symbolic");
    GtkWidget *delete_label = gtk_label_new("Delete");
    gtk_box_append(GTK_BOX(delete_box), icon);
    gtk_box_append(GTK_BOX(delete_box), delete_label);
    gtk_button_set_child(GTK_BUTTON(delete_button), delete_box);
    gtk_widget_add_css_class(delete_button, "sidebar-button");
    gtk_widget_set_sensitive(delete_button, FALSE);

    // 5. Pack buttons into button_hbox
    gtk_widget_set_hexpand(browse_button, TRUE);
    gtk_widget_set_halign(browse_button, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(button_hbox), browse_button);
    gtk_box_append(GTK_BOX(button_hbox), delete_button);

    // 6. Create list box
    scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    list_box = gtk_list_box_new();
    gtk_widget_set_name(list_box, "file-list-box"); // For CSS
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), list_box);

    // 7. Create and fill the SidebarData struct
    SidebarData *callback_data = g_new(SidebarData, 1);
    callback_data->parent_window = parent_window;
    callback_data->list_box = list_box;
    callback_data->delete_button = delete_button;

    // 8. Connect all signals
    g_signal_connect(browse_button, "clicked", G_CALLBACK(on_browse_clicked), callback_data);
    g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_clicked), callback_data);
    g_signal_connect(list_box, "row-selected", G_CALLBACK(on_row_selected), callback_data);
    g_signal_connect(sidebar_vbox, "destroy", G_CALLBACK(g_free), callback_data);

    // 9. Pack main sidebar
    gtk_box_append(GTK_BOX(sidebar_vbox), button_hbox);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(sidebar_vbox), scrolled_window);

    /* Load persisted files list from data/files_index.txt */
    const char *data_dir = "data";
    gchar *index_path = g_build_filename(data_dir, "files_index.txt", NULL);
    if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {
        FILE *f = fopen(index_path, "r");
        if (f) {
            char buf[4096];
            while (fgets(buf, sizeof(buf), f)) {
                char *nl = strchr(buf, '\n'); if (nl) *nl = '\0';
                if (buf[0] != '\0') {
                    add_path_to_list(callback_data, buf);
                }
            }
            fclose(f);
        }
    }
    g_free(index_path);

    return sidebar_vbox;
}