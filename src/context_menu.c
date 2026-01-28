#include <gtk/gtk.h>
#include "context_menu.h"
#include "diff_view.h"
#include <stdio.h> // For printf
#include <gio/gio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#if defined(__has_include)
# if __has_include(<json-glib/json-glib.h>)
#  include <json-glib/json-glib.h>
#  define HAVE_JSON_GLIB 1
# endif
#endif
#if defined(G_OS_WIN32) || defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#endif

// ---
// --- Globals for version comparison
// ---

// List to store pointers to selected version widgets for comparison
static GList *selected_for_comparison = NULL;

// ---
// --- CONTEXT 1: "sidebar-element" Actions
// ---

static void open(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);

    const char *stored_path = g_object_get_data(G_OBJECT(widget), "file-path");
    const char *path = stored_path ? stored_path : gtk_widget_get_name(widget);

    if (path == NULL) {
        g_printerr("Open: no path available for widget\n");
        return;
    }

    g_print("Open: requested path='%s'\n", path);

    // Ensure we have an absolute, canonical path
    char *abs_path = g_canonicalize_filename(path, NULL);
    if (abs_path == NULL) {
        g_printerr("Open: failed to canonicalize path '%s'\n", path);
        return;
    }

    // Check the file exists
    if (!g_file_test(abs_path, G_FILE_TEST_EXISTS)) {
        g_printerr("Open: file does not exist: '%s'\n", abs_path);
        g_free(abs_path);
        return;
    }

    // --- Windows native launch (preferred) ---
#if defined(G_OS_WIN32)
    {
        gunichar2 *wpath = g_utf8_to_utf16(abs_path, -1, NULL, NULL, NULL);
        if (wpath) {
            HINSTANCE res = ShellExecuteW(NULL, L"open", (LPCWSTR)wpath, NULL, NULL, SW_SHOWNORMAL);
            g_free(wpath);
            if ((intptr_t)res > 32) {
                g_print("Open: launched via ShellExecuteW: %s\n", abs_path);
                g_free(abs_path);
                return;
            } else {
                g_printerr("Open: ShellExecuteW failed (code=%ld), falling back to GAppInfo\n", (long)(intptr_t)res);
            }
        } else {
            g_printerr("Open: failed to convert path to UTF-16, falling back to GAppInfo\n");
        }
    }
#endif

    // Convert to file:// URI
    GError *error = NULL;
    char *uri = g_filename_to_uri(abs_path, NULL, &error);
    if (uri == NULL) {
        g_printerr("Open: failed to build URI for '%s': %s\n", abs_path, error ? error->message : "unknown");
        g_clear_error(&error);
        g_free(abs_path);
        return;
    }

    g_print("Open: trying g_app_info_launch_default_for_uri('%s')\n", uri);

    gboolean launched = g_app_info_launch_default_for_uri(uri, NULL, &error);
    if (launched) {
        g_print("Open: launched via GAppInfo: %s\n", uri);
        g_free(uri);
        g_free(abs_path);
        return;
    }

    // If we get here, GAppInfo failed. Print error and try a fallback.
    g_printerr("Open: g_app_info_launch_default_for_uri failed for '%s': %s\n",
               uri, error ? error->message : "unknown");
    g_clear_error(&error);

    // --- Platform fallback: try shell open / start commands ---
#if defined(G_OS_WIN32)
    {
        // Use cmd.exe /c start "" "C:\path\to\file.png"
        gchar *cmd = g_strdup_printf("cmd.exe /c start \"\" \"%s\"", abs_path);
        if (!g_spawn_command_line_async(cmd, &error)) {
            g_printerr("Open fallback: cmd start failed: %s\n", error ? error->message : "unknown");
            g_clear_error(&error);
        } else {
            g_print("Open: launched via cmd start: %s\n", abs_path);
        }
        g_free(cmd);
    }
#elif defined(__APPLE__)
    {
        gchar *cmd = g_strdup_printf("open \"%s\"", abs_path);
        if (!g_spawn_command_line_async(cmd, &error)) {
            g_printerr("Open fallback: open failed: %s\n", error ? error->message : "unknown");
            g_clear_error(&error);
        } else {
            g_print("Open: launched via open: %s\n", abs_path);
        }
        g_free(cmd);
    }
#else
    {
        // Linux / other Unix: try xdg-open
        gchar *cmd = g_strdup_printf("xdg-open \"%s\"", abs_path);
        if (!g_spawn_command_line_async(cmd, &error)) {
            g_printerr("Open fallback: xdg-open failed: %s\n", error ? error->message : "unknown");
            g_clear_error(&error);
        } else {
            g_print("Open: launched via xdg-open: %s\n", abs_path);
        }
        g_free(cmd);
    }
#endif

    g_free(uri);
    g_free(abs_path);
}

typedef struct {
    GtkWidget *dialog;
    GtkWidget *entry;
    GtkWidget *target_widget; /* the label widget representing the file */
} RenameData;

static void on_rename_response(GtkDialog *dialog, int response_id, gpointer user_data) {
    RenameData *rd = (RenameData *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        gchar *new_name = NULL;
        g_object_get(G_OBJECT(rd->entry), "text", &new_name, NULL);
        if (new_name && *new_name) {
            const char *old_path = g_object_get_data(G_OBJECT(rd->target_widget), "file-path");
            if (old_path == NULL) {
                g_printerr("Rename: no original path stored on widget\n");
            } else {
                char *dir = g_path_get_dirname(old_path);
                char *new_path = g_build_filename(dir, new_name, NULL);

                GFile *src = g_file_new_for_path(old_path);
                GFile *dest = g_file_new_for_path(new_path);
                GError *error = NULL;
                gboolean moved = g_file_move(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
                if (!moved) {
                    g_printerr("Rename failed: %s\n", error ? error->message : "unknown");
                    g_clear_error(&error);
                } else {
                    /* Update stored path (g_object_set_data_full will handle freeing the previous value) */
                    g_object_set_data_full(G_OBJECT(rd->target_widget), "file-path", g_strdup(new_path), g_free);

                    /* Update widget name and label text */
                    gtk_widget_set_name(rd->target_widget, new_name);
                    /* The row stores a pointer to the label widget */
                    GtkWidget *label = g_object_get_data(G_OBJECT(rd->target_widget), "label-widget");
                    if (label && GTK_IS_LABEL(label)) {
                        gtk_label_set_text(GTK_LABEL(label), new_name);
                    }
                }

                g_object_unref(src);
                g_object_unref(dest);
                g_free(dir);
                g_free(new_path);
            }
            g_free(new_name);
        }
    }

    gtk_window_destroy(GTK_WINDOW(rd->dialog));
    g_free(rd);
}

static void on_rename_ok_clicked(GtkButton *button, gpointer user_data) {
    RenameData *rd = (RenameData *)user_data;
    on_rename_response(GTK_DIALOG(rd->dialog), GTK_RESPONSE_ACCEPT, rd);
}

static void on_rename_cancel_clicked(GtkButton *button, gpointer user_data) {
    RenameData *rd = (RenameData *)user_data;
    on_rename_response(GTK_DIALOG(rd->dialog), GTK_RESPONSE_CANCEL, rd);
}

static void _rename(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    /* user_data is the GtkWidget (the label) we passed in. */
    GtkWidget *widget = GTK_WIDGET(user_data);

    /* Get parent window to be transient for */
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW));

    RenameData *rd = g_new0(RenameData, 1);
    rd->target_widget = widget;

    /* Create a transient window with entry and buttons (GTK4-friendly) */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Rename File");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);

    GtkWidget *entry = gtk_entry_new();
    rd->entry = entry;

    /* Pre-fill with current basename */
    const char *old_path = g_object_get_data(G_OBJECT(widget), "file-path");
    char *base = NULL;
    if (old_path) base = g_path_get_basename(old_path);
    else {
        const char *wname = gtk_widget_get_name(widget);
        if (wname) base = g_strdup(wname);
    }
    if (base) {
        g_object_set(G_OBJECT(entry), "text", base, NULL);
        g_free(base);
    }

    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *ok = gtk_button_new_with_label("Rename");
    gtk_box_append(GTK_BOX(hbox), cancel);
    gtk_box_append(GTK_BOX(hbox), ok);

    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    rd->dialog = dialog;

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_rename_cancel_clicked), rd);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_rename_ok_clicked), rd);

    /* Show dialog */
    gtk_window_present(GTK_WINDOW(dialog));
}
/* Helper that performs the actual deletion of a sidebar row and index update */
static void perform_delete_row(GtkWidget *row) {
    if (!row) return;
    const char *path = g_object_get_data(G_OBJECT(row), "file-path");
    g_print("perform_delete_row: row=%p path=%s\n", row, path ? path : "(null)");

    /* Try to remove the file from disk first */
    if (path) {
        if (remove(path) != 0) {
            int err = errno;
            const char *errstr = strerror(err);
            /* Show an error dialog and abort deletion */
            GtkWidget *toplevel = gtk_widget_get_ancestor(row, GTK_TYPE_WINDOW);
            GtkWindow *parent_window = toplevel ? GTK_WINDOW(toplevel) : NULL;
            GtkWidget *dialog = gtk_window_new();
            gtk_window_set_title(GTK_WINDOW(dialog), "Delete Failed");
            if (parent_window) gtk_window_set_transient_for(GTK_WINDOW(dialog), parent_window);
            gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
            GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_set_margin_top(vbox, 8);
            gtk_widget_set_margin_bottom(vbox, 8);
            gtk_widget_set_margin_start(vbox, 8);
            gtk_widget_set_margin_end(vbox, 8);
            gchar *msg = g_strdup_printf("Failed to delete '%s': %s", path, errstr ? errstr : "unknown");
            GtkWidget *label = gtk_label_new(msg);
            g_free(msg);
            gtk_box_append(GTK_BOX(vbox), label);
            GtkWidget *ok = gtk_button_new_with_label("OK");
            g_signal_connect(ok, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
            gtk_box_append(GTK_BOX(vbox), ok);
            gtk_window_set_child(GTK_WINDOW(dialog), vbox);
            gtk_window_present(GTK_WINDOW(dialog));
            g_printerr("perform_delete_row: failed to remove file %s: %s\n", path, errstr ? errstr : "unknown");
            return;
        }
        g_print("perform_delete_row: removed file from disk: %s\n", path);
    }

    /* Remove row from UI */
    GtkWidget *parent = gtk_widget_get_parent(row);
    if (GTK_IS_LIST_BOX(parent)) {
        gtk_list_box_remove(GTK_LIST_BOX(parent), row);
        g_print("perform_delete_row: removed row from list box\n");
    } else {
        gtk_widget_unparent(row);
        g_print("perform_delete_row: unparented row\n");
    }

    /* Remove from data/files_index.txt */
    if (path) {
        const char *data_dir = "data";
        gchar *index_path = g_build_filename(data_dir, "files_index.txt", NULL);
        if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {
            GPtrArray *lines = g_ptr_array_new_with_free_func(g_free);
            FILE *f = fopen(index_path, "r");
            if (f) {
                char buf[4096];
                while (fgets(buf, sizeof(buf), f)) {
                    char *nl = strchr(buf, '\n'); if (nl) *nl = '\0';
                    if (g_strcmp0(buf, path) != 0) {
                        g_ptr_array_add(lines, g_strdup(buf));
                    }
                }
                fclose(f);
            }
            /* write back */
            FILE *fw = fopen(index_path, "w");
            if (fw) {
                for (guint i = 0; i < lines->len; ++i) {
                    fprintf(fw, "%s\n", (char *)g_ptr_array_index(lines, i));
                }
                fclose(fw);
            }
            g_ptr_array_free(lines, TRUE);
            g_print("perform_delete_row: updated files_index.txt\n");
        }
        g_free(index_path);
    }

    /* Hide versions list if present on the same toplevel window */
    GtkWidget *toplevel = gtk_widget_get_ancestor(row, GTK_TYPE_WINDOW);
    if (toplevel) {
        GtkWidget *versions_list = g_object_get_data(G_OBJECT(toplevel), "versions-list");
        if (versions_list) {
            extern void clear_list_box_widget(GtkWidget *box_widget);
            clear_list_box_widget(versions_list);
            gtk_widget_set_visible(versions_list, FALSE);
            g_print("perform_delete_row: cleared and hid versions list\n");
        }
    }
}

typedef struct { GtkWidget *row; GtkWidget *dialog; } DeleteConfirmData;

static void on_delete_confirm_clicked(GtkButton *button, gpointer user_data) {
    DeleteConfirmData *d = (DeleteConfirmData *)user_data;
    if (d && d->row) perform_delete_row(d->row);
    if (d && d->dialog) gtk_window_destroy(GTK_WINDOW(d->dialog));
    g_free(d);
}

static void on_delete_cancel_clicked(GtkButton *button, gpointer user_data) {
    DeleteConfirmData *d = (DeleteConfirmData *)user_data;
    if (d && d->dialog) gtk_window_destroy(GTK_WINDOW(d->dialog));
    g_free(d);
}

static void delete_file(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWidget *row = GTK_WIDGET(user_data);
    if (!row) return;

    GtkWidget *toplevel = gtk_widget_get_ancestor(row, GTK_TYPE_WINDOW);
    GtkWindow *parent_window = toplevel ? GTK_WINDOW(toplevel) : NULL;

    /* Build simple confirmation dialog */
    DeleteConfirmData *d = g_new0(DeleteConfirmData, 1);
    d->row = row;

    GtkWidget *dialog = gtk_window_new();
    d->dialog = dialog;
    gtk_window_set_title(GTK_WINDOW(dialog), "Delete File?");
    if (parent_window) gtk_window_set_transient_for(GTK_WINDOW(dialog), parent_window);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);

    const char *path = g_object_get_data(G_OBJECT(row), "file-path");
    const char *name = NULL;
    if (path) name = g_path_get_basename(path);

    /* Main title: Delete 'name'? */
    gchar *title = NULL;
    if (name) title = g_strdup_printf("Delete '%s'?", name);
    else title = g_strdup("Delete this file?");
    GtkWidget *title_label = gtk_label_new(title);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    g_free(title);
    gtk_box_append(GTK_BOX(vbox), title_label);

    /* Sub-label: full path in smaller, dim font using Pango markup */
    if (path) {
        gchar *markup = g_markup_printf_escaped("<span size=\"small\" foreground=\"#666666\">%s</span>", path);
        GtkWidget *path_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(path_label), markup);
        gtk_widget_set_halign(path_label, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(path_label), 0.0);
        gtk_box_append(GTK_BOX(vbox), path_label);
        g_free(markup);
    }
    if (name) g_free((gchar*)name);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *del = gtk_button_new_with_label("Delete");
    gtk_box_append(GTK_BOX(hbox), cancel);
    gtk_box_append(GTK_BOX(hbox), del);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_delete_cancel_clicked), d);
    g_signal_connect(del, "clicked", G_CALLBACK(on_delete_confirm_clicked), d);

    gtk_window_present(GTK_WINDOW(dialog));
}

/* Record a version: copy the current file into data/versions and append index */
static void record_version(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);
    const char *path = g_object_get_data(G_OBJECT(widget), "file-path");
    if (!path) { g_printerr("record_version: no file path\n"); return; }

    const char *data_dir = "data";
    gchar *versions_dir = g_build_filename(data_dir, "versions", NULL);
    g_mkdir_with_parents(versions_dir, 0755);

    // build timestamped filename and preserve extension
    time_t t = time(NULL);
    struct tm tminfo;
#if defined(_WIN32) || defined(__MINGW32__)
    localtime_s(&tminfo, &t);
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    localtime_r(&t, &tminfo);
#else
    {
        struct tm *tmp = localtime(&t);
        if (tmp) tminfo = *tmp; else memset(&tminfo, 0, sizeof(tminfo));
    }
#endif
    char timestr[64]; strftime(timestr, sizeof(timestr), "%Y%m%d%H%M%S", &tminfo);

    gchar *base = g_path_get_basename(path);
    gchar *safe_base = g_strdup(base);
    for (char *p = safe_base; *p; ++p) if (*p == '/' || *p == '\\') *p = '_';
    /* extract extension manually to avoid missing glib API on some systems */
    const char *ext = NULL;
    char *dot = strrchr(base, '.');
    if (dot && dot[1] != '\0') ext = dot + 1;
    gchar *dest_name;
    if (ext && *ext) dest_name = g_strdup_printf("%s_%s.%s", safe_base, timestr, ext);
    else dest_name = g_strdup_printf("%s_%s", safe_base, timestr);
    gchar *dest_path = g_build_filename(versions_dir, dest_name, NULL);

    GError *error = NULL;
    GFile *src = g_file_new_for_path(path);
    GFile *dest = g_file_new_for_path(dest_path);
    if (!g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
        g_printerr("record_version: copy failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
#ifdef HAVE_JSON_GLIB
        /* Update JSON index: data/versions_index.json */
        gchar *index_path = g_build_filename(data_dir, "versions_index.json", NULL);
        JsonParser *parser = json_parser_new();
        JsonArray *arr = NULL;
        if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {
            if (json_parser_load_from_file(parser, index_path, &error)) {
                JsonNode *root = json_parser_get_root(parser);
                if (JSON_NODE_HOLDS_ARRAY(root)) arr = json_node_get_array(root);
            } else {
                g_clear_error(&error);
            }
        }
        if (!arr) arr = json_array_new();

        JsonObject *obj = json_object_new();
        json_object_set_string_member(obj, "original", path);
        json_object_set_string_member(obj, "stored", dest_name);
        json_object_set_string_member(obj, "timestamp", timestr);

        JsonNode *obj_node = json_node_new(JSON_NODE_OBJECT);
        json_node_set_object(obj_node, obj);
        json_array_add_element(arr, obj_node);

        /* write back */
        JsonGenerator *gen = json_generator_new();
        JsonNode *root_node = json_node_new(JSON_NODE_ARRAY);
        json_node_set_array(root_node, arr);
        json_generator_set_root(gen, root_node);
        if (!json_generator_to_file(gen, index_path, &error)) {
            g_printerr("Failed to write versions index: %s\n", error ? error->message : "unknown");
            g_clear_error(&error);
        }

        if (gen) g_object_unref(gen);
        if (root_node) json_node_free(root_node);
        if (parser) g_object_unref(parser);

        g_free(index_path);
#else
        /* Fallback: append to text index versions_index.txt (original|stored|timestamp) */
        gchar *index_path = g_build_filename(data_dir, "versions_index.txt", NULL);
        FILE *fi = fopen(index_path, "a");
        if (fi) {
            fprintf(fi, "%s|%s|%s\n", path, dest_name, timestr);
            fclose(fi);
        }
        g_free(index_path);
#endif

        /* update UI: find versions-list on toplevel and repopulate */
        GtkWidget *toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
        if (toplevel) {
            GtkWidget *versions_list = g_object_get_data(G_OBJECT(toplevel), "versions-list");
            if (versions_list) {
                extern void populate_versions_for_path(GtkWindow *parent, GtkListBox *versions_list, const char *original_path);
                populate_versions_for_path(GTK_WINDOW(toplevel), GTK_LIST_BOX(versions_list), path);
            }
        }
    }

    g_free(versions_dir);
    g_free(safe_base);
    g_free(dest_name);
    g_free(dest_path);
    if (base) g_free(base);
    if (src) g_object_unref(src);
    if (dest) g_object_unref(dest);
}

// An array of actions for the "sideabar-element" context
static const GActionEntry sidebar_element_menu_actions[] = {
    {"open_file", open, NULL, NULL, NULL},
    {"record_version", record_version, NULL, NULL, NULL},
    {"delete_file", delete_file, NULL, NULL, NULL},
    {"rename_file",  _rename,  NULL, NULL, NULL}
};

/* Actions for a version row (right pane) */
static void open_version(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    /* user_data will be the version row widget */
    open(action, parameter, user_data);
}

static void clear_comparison_selection() {
    for (GList *l = selected_for_comparison; l != NULL; l = l->next) {
        GtkWidget *widget = GTK_WIDGET(l->data);
        gtk_widget_remove_css_class(widget, "selected-for-compare");
    }
    g_list_free_full(selected_for_comparison, g_object_unref);
    selected_for_comparison = NULL;
}

static void compare_versions(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    if (g_list_length(selected_for_comparison) != 2) {
        g_printerr("Compare action should not be available\n");
        return;
    }

    GtkWidget *widget1 = GTK_WIDGET(selected_for_comparison->data);
    GtkWidget *widget2 = GTK_WIDGET(selected_for_comparison->next->data);

    const char *path1 = g_object_get_data(G_OBJECT(widget1), "file-path");
    const char *path2 = g_object_get_data(G_OBJECT(widget2), "file-path");

    if (path1 && path2) {
        g_print("Comparing '%s' and '%s'\n", path1, path2);
        GtkWindow *parent = GTK_WINDOW(gtk_widget_get_ancestor(widget1, GTK_TYPE_WINDOW));
        create_diff_window(parent, path1, path2, GTK_LIST_BOX_ROW(widget2));
    } else {
        g_printerr("Could not get paths for comparison\n");
    }
    clear_comparison_selection();
}

static void select_for_comparison(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);

    if (g_list_find(selected_for_comparison, widget)) {
        // Already selected, so unselect it
        gtk_widget_remove_css_class(widget, "selected-for-compare");
        selected_for_comparison = g_list_remove(selected_for_comparison, widget);
        g_object_unref(widget);
    } else {
        // Not selected, so add it
        gtk_widget_add_css_class(widget, "selected-for-compare");
        selected_for_comparison = g_list_prepend(selected_for_comparison, widget);
        g_object_ref(widget);

        // If we now have more than 2 items, remove the oldest one
        if (g_list_length(selected_for_comparison) > 2) {
            GList *last = g_list_last(selected_for_comparison);
            GtkWidget *last_widget = GTK_WIDGET(last->data);
            gtk_widget_remove_css_class(last_widget, "selected-for-compare");
            g_object_unref(last_widget);
            selected_for_comparison = g_list_delete_link(selected_for_comparison, last);
        }
    }
}

// Data for repopulating versions list after deletion
typedef struct {
    GtkWindow *window;
    GtkListBox *versions_list;
    gchar *original_path;
} RepopulateData;

static gboolean repopulate_versions_idle(gpointer user_data) {
    RepopulateData *data = (RepopulateData *)user_data;
    if (data && data->window && data->versions_list && data->original_path) {
        extern void populate_versions_for_path(GtkWindow *parent, GtkListBox *versions_list, const char *original_path);
        populate_versions_for_path(data->window, data->versions_list, data->original_path);
    }
    if (data) {
        g_free(data->original_path);
        g_free(data);
    }
    return G_SOURCE_REMOVE;
}

static void delete_version(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWidget *row = GTK_WIDGET(user_data);
    const char *vpath = g_object_get_data(G_OBJECT(row), "version-path");
    if (!vpath) return;

    // Store information we need before destroying anything
    GtkWidget *toplevel = gtk_widget_get_ancestor(row, GTK_TYPE_WINDOW);
    const char *original_path = NULL;
    GtkWidget *versions_list = NULL;
    
    if (toplevel) {
        original_path = g_object_get_data(G_OBJECT(toplevel), "original-path");
        versions_list = g_object_get_data(G_OBJECT(toplevel), "versions-list");
    }

    // Duplicate the path since vpath is stored on the row which will be destroyed
    gchar *vpath_copy = g_strdup(vpath);
    
    // Destroy the popover menu before attempting to remove the row
    GtkWidget *popover = g_object_get_data(G_OBJECT(row), "popover");
    if (popover && GTK_IS_WIDGET(popover)) {
        gtk_popover_popdown(GTK_POPOVER(popover));
        gtk_widget_unparent(popover);
        g_object_set_data(G_OBJECT(row), "popover", NULL);
    }

    // Try to remove the file (use _wremove on Windows for better Unicode support)
#if defined(_WIN32) || defined(__MINGW32__)
    wchar_t *wpath = g_utf8_to_utf16(vpath_copy, -1, NULL, NULL, NULL);
    int result = -1;
    if (wpath) {
        result = _wremove(wpath);
        g_free(wpath);
    }
#else
    int result = remove(vpath_copy);
#endif

    if (result == 0) {
        g_print("delete_version: successfully removed %s\n", vpath_copy);

        gchar *stored_basename = g_path_get_basename(vpath_copy);

#ifdef HAVE_JSON_GLIB
        /* Update JSON index: data/versions_index.json */
        const char *data_dir = "data";
        gchar *index_path = g_build_filename(data_dir, "versions_index.json", NULL);
        JsonParser *parser = json_parser_new();
        JsonArray *arr = NULL;
        GError *error = NULL;

        if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {
            if (json_parser_load_from_file(parser, index_path, &error)) {
                JsonNode *root = json_parser_get_root(parser);
                if (JSON_NODE_HOLDS_ARRAY(root)) {
                    arr = json_node_get_array(root);
                    JsonArray *new_arr = json_array_new();

                    for (guint i = 0; i < json_array_get_length(arr); i++) {
                        JsonNode *elem = json_array_get_element(arr, i);
                        if (JSON_NODE_HOLDS_OBJECT(elem)) {
                            JsonObject *obj = json_node_get_object(elem);
                            const char *stored = json_object_get_string_member(obj, "stored");
                            if (g_strcmp0(stored, stored_basename) != 0) {
                                json_array_add_element(new_arr, json_node_copy(elem));
                            }
                        }
                    }

                    // Write back the new array
                    JsonGenerator *gen = json_generator_new();
                    JsonNode *root_node = json_node_new(JSON_NODE_ARRAY);
                    json_node_set_array(root_node, new_arr);
                    json_generator_set_root(gen, root_node);
                    json_generator_to_file(gen, index_path, NULL);
                    g_object_unref(gen);
                    json_node_free(root_node);
                }
            } else {
                g_clear_error(&error);
            }
        }
        if (parser) g_object_unref(parser);
        g_free(index_path);
#else
        /* Fallback index: data/versions_index.txt */
        const char *data_dir = "data";
        gchar *index_path = g_build_filename(data_dir, "versions_index.txt", NULL);
        if (stored_basename && g_file_test(index_path, G_FILE_TEST_EXISTS)) {
            GPtrArray *lines = g_ptr_array_new_with_free_func(g_free);
            FILE *f = fopen(index_path, "r");
            if (f) {
                char buf[4096];
                while (fgets(buf, sizeof(buf), f)) {
                    char *trimmed = g_strdup(buf);
                    if (!trimmed) continue;
                    char *nl = strpbrk(trimmed, "\r\n");
                    if (nl) *nl = '\0';

                    gboolean keep = TRUE;
                    char *sep1 = strchr(trimmed, '|');
                    if (sep1) {
                        char *sep2 = strchr(sep1 + 1, '|');
                        if (sep2) {
                            *sep2 = '\0';
                            const char *stored = sep1 + 1;
                            if (g_strcmp0(stored, stored_basename) == 0) {
                                keep = FALSE;
                            }
                            *sep2 = '|';
                        }
                    }

                    if (keep) {
                        g_ptr_array_add(lines, trimmed);
                    } else {
                        g_free(trimmed);
                    }
                }
                fclose(f);
            }

            FILE *fw = fopen(index_path, "w");
            if (fw) {
                for (guint i = 0; i < lines->len; ++i) {
                    const char *line = g_ptr_array_index(lines, i);
                    fprintf(fw, "%s\n", line);
                }
                fclose(fw);
            }

            g_ptr_array_free(lines, TRUE);
        }
        g_free(index_path);
#endif

        if (stored_basename) g_free(stored_basename);

        /* Schedule repopulation in an idle callback to avoid issues with widget destruction */
        if (toplevel && original_path && versions_list) {
            RepopulateData *data = g_new0(RepopulateData, 1);
            data->window = GTK_WINDOW(toplevel);
            data->versions_list = GTK_LIST_BOX(versions_list);
            data->original_path = g_strdup(original_path);
            g_idle_add(repopulate_versions_idle, data);
        }
    } else {
        int err = errno;
        g_printerr("delete_version: failed to remove %s: %s (errno=%d)\n", 
                   vpath_copy, strerror(err), err);
    }
    
    g_free(vpath_copy);
}

static const GActionEntry version_element_menu_actions[] = {
    {"open_version", open_version, NULL, NULL, NULL},
    {"delete_version", delete_version, NULL, NULL, NULL},
    {"select_for_comparison", select_for_comparison, NULL, NULL, NULL},
    {"compare_versions", compare_versions, NULL, NULL, NULL}
};

// ---
// --- Public Function (The "Router")
// ---

void on_widget_right_click(GtkGestureClick *gesture,
                           int n_press,
                           double x,
                           double y,
                           gpointer user_data) {
    
    // --- 1. Get Context and Widget ---
    
    // Get the widget that the gesture is attached to
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    
    // Get the top-level window to add actions to
    GtkWidget *toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

    // This is the MOST IMPORTANT line:
    // We cast the generic 'user_data' pointer to the string we passed.
    const char *context = (const char *)user_data;

    // Create the GMenu model. We *must* create it *before* the if-block.
    GMenu *menu_model = g_menu_new();


    // --- 2. The "Router" Logic ---
    //
    // We check the context string and build the *correct* menu.
    // g_strcmp0 is a safe way to compare strings (it handles NULL).

    if (g_strcmp0(context, "sidebar-element") == 0) {
        
        // --- Build "Label" Menu ---
        printf("Displaying 'sidebar-element' menu\n");
        
        // Add this context's actions to the window
        g_action_map_add_action_entries(G_ACTION_MAP(toplevel),
                                        sidebar_element_menu_actions,
                                        G_N_ELEMENTS(sidebar_element_menu_actions),
                                        widget); // Pass widget to the actions
        
        // Build the menu model
    g_menu_append(menu_model, "Open File", "win.open_file");
    g_menu_append(menu_model, "Record This Version", "win.record_version");
    g_menu_append(menu_model, "Rename File", "win.rename_file");
    g_menu_append(menu_model, "Delete File", "win.delete_file");
    clear_comparison_selection();

    }
    else if (g_strcmp0(context, "version-element") == 0) {
        printf("Displaying 'version-element' menu\n");
        g_action_map_add_action_entries(G_ACTION_MAP(toplevel),
                                        version_element_menu_actions,
                                        G_N_ELEMENTS(version_element_menu_actions),
                                        widget);
        g_menu_append(menu_model, "Open Version", "win.open_version");
        g_menu_append(menu_model, "Select for Compare", "win.select_for_comparison");
        
        // The 'Compare' item is only enabled if exactly two items are selected
        if (g_list_length(selected_for_comparison) == 2) {
             g_menu_append(menu_model, "Compare", "win.compare_versions");
        }
       
        g_menu_append(menu_model, "Delete Version", "win.delete_version");
    }
    else {
        
        // --- Build "Default" or "Error" Menu ---
        printf("Displaying default menu (unknown context: '%s')\n", context);
        
        // No actions to add, just build a simple model
        g_menu_append(menu_model, "No actions for this widget", NULL); // NULL = greyed out
        clear_comparison_selection();
    }


    // --- 3. Common Popover Display Logic ---
    //
    // This code is the same no matter *which* menu we built.
    
    // First, check if there's an existing popover and clean it up
    GtkWidget *old_popover = g_object_get_data(G_OBJECT(widget), "popover");
    if (old_popover) {
        gtk_widget_unparent(old_popover);
        g_object_set_data(G_OBJECT(widget), "popover", NULL);
    }
    
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu_model));
    g_object_set_data(G_OBJECT(widget), "popover", popover);
    gtk_widget_set_parent(popover, widget);
    
    GdkRectangle pointing_rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &pointing_rect);
    
    gtk_popover_popup(GTK_POPOVER(popover));

    // We are done with our reference to the model, so we unref it.
    // The popover still holds its own reference.
    g_object_unref(menu_model);
}