#include "diff_view.h"
#include "diff_logic.h"
#include <gtk/gtk.h>
#include <string.h>
#include <gio/gio.h>
#include "sidebar.h"
#if defined(__has_include)
# if __has_include(<json-glib/json-glib.h>)
#  include <json-glib/json-glib.h>
#  define HAVE_JSON_GLIB 1
# endif
#endif
#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#endif

/* Struct to hold data for revert confirmation callback */
typedef struct {
    GtkWindow *diff_window;
    gchar *latest_file_path;
    gchar *original_file_path;
    GtkWindow *parent_window;
    GtkListBox *versions_list;
    GtkListBoxRow *version_row;  /* The version row to remove from UI */
} RevertData;

/* Forward declarations */

static void on_revert_confirm_clicked(GtkButton *button, gpointer user_data);

static void on_revert_cancel_clicked(GtkButton *button, gpointer user_data);



/* Helper function to remove a version file from the index */

static void remove_version_from_index(const char *version_file_path) {

    const char *data_dir = "data";

    gchar *basename = g_path_get_basename(version_file_path);

    

#ifdef HAVE_JSON_GLIB

    gchar *index_path = g_build_filename(data_dir, "versions_index.json", NULL);

    if (g_file_test(index_path, G_FILE_TEST_EXISTS)) {

        // JSON index handling

        GError *error = NULL;

        JsonParser *parser = json_parser_new();

        if (json_parser_load_from_file(parser, index_path, &error)) {

            JsonNode *root = json_parser_get_root(parser);

            if (JSON_NODE_HOLDS_ARRAY(root)) {

                JsonArray *array = json_node_get_array(root);

                guint len = json_array_get_length(array);

                for (guint i = 0; i < len; i++) {

                    JsonNode *node = json_array_get_element(array, i);

                    if (JSON_NODE_HOLDS_OBJECT(node)) {

                        JsonObject *obj = json_node_get_object(node);

                        const char *stored = json_object_get_string_member(obj, "stored");

                        if (g_strcmp0(stored, basename) == 0) {

                            json_array_remove_element(array, i);

                            break;

                        }

                    }

                }

                JsonGenerator *generator = json_generator_new();

                json_generator_set_root(generator, root);

                json_generator_to_file(generator, index_path, NULL);

                g_object_unref(generator);

            }

        }

        g_object_unref(parser);

        if (error) g_error_free(error);

    }

    g_free(index_path);

#endif



    /* Fallback: use text-based index */

    gchar *index_path_txt = g_build_filename(data_dir, "versions_index.txt", NULL);

    if (g_file_test(index_path_txt, G_FILE_TEST_EXISTS)) {

        FILE *f = fopen(index_path_txt, "r");

        GString *new_contents = g_string_new("");

        if (f) {

            char line[4096];

            while (fgets(line, sizeof(line), f)) {

                // Parse the line: orig|stored|ts

                char *copy = g_strdup(line);

                char *nl = strchr(copy, '\n');

                if (nl) *nl = '\0';

                char *p1 = strchr(copy, '|');

                if (!p1) { g_free(copy); continue; }

                *p1 = '\0';

                char *p2 = strchr(p1 + 1, '|');

                if (!p2) { g_free(copy); continue; }

                *p2 = '\0';

                const char *stored = p1 + 1;

                if (g_strcmp0(stored, basename) != 0) {

                    g_string_append(new_contents, line);

                }

                g_free(copy);

            }

            fclose(f);

        }

        // Write back the file

        GError *error = NULL;

        g_file_set_contents(index_path_txt, new_contents->str, new_contents->len, &error);

        if (error) {

            g_printerr("Error writing index file: %s\n", error->message);

            g_error_free(error);

        }

        g_string_free(new_contents, TRUE);

    }

    g_free(index_path_txt);

    g_free(basename);

}



/* Revert confirm callback - performs the actual revert */

static void on_revert_confirm_clicked(GtkButton *button, gpointer user_data) {

    RevertData *data = (RevertData *)user_data;
    
    if (!data || !data->latest_file_path || !data->original_file_path) {
        if (data) {
            g_free(data->latest_file_path);
            g_free(data->original_file_path);
        }
        g_free(data);
        return;
    }

    g_print("Reverting: copying %s to %s\n", data->latest_file_path, data->original_file_path);

    /* Copy the versioned file over the original file */
    GFile *src = g_file_new_for_path(data->latest_file_path);
    GFile *dest = g_file_new_for_path(data->original_file_path);
    GError *error = NULL;

    if (!g_file_copy(src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
        g_printerr("Error reverting file: %s\n", error->message);
        g_error_free(error);
        
        /* Clean up and return without deleting */
        g_object_unref(src);
        g_object_unref(dest);
        g_free(data->latest_file_path);
        g_free(data->original_file_path);
        g_free(data);
        
        /* Optionally, show an error dialog to the user */
        GtkAlertDialog *alert_dialog = gtk_alert_dialog_new("Failed to revert file. Please check permissions.");
        gtk_alert_dialog_show(alert_dialog, data->diff_window);
        g_object_unref(alert_dialog);
        
        return;
    }

    g_object_unref(src);
    g_object_unref(dest);

    g_print("Revert successful. Now deleting old version: %s\n", data->latest_file_path);

    /* Delete the latest version file */
#if defined(_WIN32) || defined(__MINGW32__)
    gunichar2 *wpath = g_utf8_to_utf16(data->latest_file_path, -1, NULL, NULL, NULL);
    if (wpath) {
        _wremove(wpath);
        g_free(wpath);
    }
#else
    remove(data->latest_file_path);
#endif

    /* Remove from versions index */
    remove_version_from_index(data->latest_file_path);

    g_print("Revert completed. Updating UI.\n");
    
    /* Remove the specific version row from the UI */
    if (data->versions_list && GTK_IS_LIST_BOX_ROW(data->version_row)) {
        gtk_list_box_remove(data->versions_list, GTK_WIDGET(data->version_row));
    }
    
    /* Close the diff window */
    if (data->diff_window) {
        gtk_window_destroy(data->diff_window);
    }

    g_free(data->latest_file_path);
    g_free(data->original_file_path);
    g_free(data);
}

/* Revert cancel callback */
static void on_revert_cancel_clicked(GtkButton *button, gpointer user_data) {
    RevertData *data = (RevertData *)user_data;
    if (data) {
        if (data->latest_file_path) g_free(data->latest_file_path);
        if (data->original_file_path) g_free(data->original_file_path);
        g_free(data);
    }
}

/* Revert button clicked - show confirmation dialog */
static void on_revert_button_clicked(GtkButton *button, gpointer user_data) {
    RevertData *data = (RevertData *)user_data;
    
    if (!data || !data->latest_file_path) {
        g_printerr("Invalid revert data\n");
        return;
    }

    /* Create confirmation dialog */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Revert and Remove Version?");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), data->diff_window);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 15);
    gtk_widget_set_margin_bottom(vbox, 15);
    gtk_widget_set_margin_start(vbox, 15);
    gtk_widget_set_margin_end(vbox, 15);

    /* Title label */
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), 
                         "<span size='large' weight='bold'>Revert to this version?</span>");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), title_label);

    /* Description label */
    gchar *basename_orig = g_path_get_basename(data->original_file_path);
    gchar *basename_version = g_path_get_basename(data->latest_file_path);
    gchar *description = g_strdup_printf(
        "This will overwrite your current file:\n<b>%s</b>\n\n"
        "with the content of the selected version:\n<b>%s</b>\n\n"
        "The old version file will then be permanently deleted. This action cannot be undone.",
        basename_orig, basename_version);
    GtkWidget *desc_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(desc_label), description);
    gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc_label);

    /* Buttons */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *revert_btn = gtk_button_new_with_label("Revert and Delete");
    gtk_widget_add_css_class(revert_btn, "destructive-action");

    gtk_box_append(GTK_BOX(button_box), cancel_btn);
    gtk_box_append(GTK_BOX(button_box), revert_btn);
    gtk_box_append(GTK_BOX(vbox), button_box);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    /* Connect button signals - use swapped to destroy dialog first */
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    g_signal_connect_swapped(revert_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    
    /* Connect our actual handlers - these will be called after destroy */
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_revert_cancel_clicked), data);
    g_signal_connect(revert_btn, "clicked", G_CALLBACK(on_revert_confirm_clicked), data);

    g_free(basename_orig);
    g_free(basename_version);
    g_free(description);

    gtk_window_present(GTK_WINDOW(dialog));
}

void create_diff_window(GtkWindow* parent, const char* file1_path, const char* file2_path, GtkListBoxRow* version_row) {
    GtkWidget *window, *main_box, *grid, *scrolled_window1, *scrolled_window2, *view1, *view2, *gutter;
    GtkWidget *label1, *label2, *header_box, *revert_button;
    GtkTextBuffer *buffer1, *buffer2;

    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Compare Files");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);

    /* Create main vertical box to hold header and content */
    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    /* Create header box with revert button */
    header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(header_box, 10);
    gtk_widget_set_margin_bottom(header_box, 10);
    gtk_widget_set_margin_start(header_box, 10);
    gtk_widget_set_margin_end(header_box, 10);

    revert_button = gtk_button_new_with_label("Revert to this Version");
    gtk_widget_set_halign(revert_button, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(header_box), revert_button);
    gtk_box_append(GTK_BOX(main_box), header_box);

    grid = gtk_grid_new();
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_widget_set_valign(grid, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(main_box), grid);

    // Add labels for file names
    gchar *basename1 = g_path_get_basename(file1_path);
    gchar *basename2 = g_path_get_basename(file2_path);
    
    label1 = gtk_label_new(basename1);
    gtk_widget_set_halign(label1, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label1, 10);
    gtk_widget_set_margin_top(label1, 5);
    gtk_widget_set_margin_bottom(label1, 5);
    gtk_grid_attach(GTK_GRID(grid), label1, 0, 0, 1, 1);
    
    label2 = gtk_label_new(basename2);
    gtk_widget_set_halign(label2, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label2, 10);
    gtk_widget_set_margin_top(label2, 5);
    gtk_widget_set_margin_bottom(label2, 5);
    gtk_grid_attach(GTK_GRID(grid), label2, 2, 0, 1, 1);
    
    g_free(basename1);
    g_free(basename2);

    // Create two text views
    view1 = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view1), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view1), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view1), TRUE);
    buffer1 = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view1));
    scrolled_window1 = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scrolled_window1, TRUE);
    gtk_widget_set_vexpand(scrolled_window1, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window1), view1);
    gtk_grid_attach(GTK_GRID(grid), scrolled_window1, 0, 1, 1, 1);

    gutter = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_size_request(gutter, 2, -1);
    gtk_grid_attach(GTK_GRID(grid), gutter, 1, 1, 1, 1);

    view2 = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view2), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view2), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view2), TRUE);
    buffer2 = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view2));
    scrolled_window2 = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scrolled_window2, TRUE);
    gtk_widget_set_vexpand(scrolled_window2, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window2), view2);
    gtk_grid_attach(GTK_GRID(grid), scrolled_window2, 2, 1, 1, 1);

    // Apply CSS for better styling
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
                                      "textview { font-family: monospace; font-size: 11pt; padding: 8px; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Create tags for highlighting differences
    // Red background for deleted words (in file1)
    gtk_text_buffer_create_tag(buffer1, "diff-delete",
                              "background", "#ffcccc",
                              "foreground", "#8b0000",
                              NULL);
    
    // Green background for added words (in file2)
    gtk_text_buffer_create_tag(buffer2, "diff-insert",
                              "background", "#ccffcc",
                              "foreground", "#006400",
                              NULL);

    // Read file contents
    gchar *contents1 = NULL, *contents2 = NULL;
    gsize length1, length2;
    
    if (!g_file_get_contents(file1_path, &contents1, &length1, NULL)) {
        g_printerr("Failed to read file: %s\n", file1_path);
        contents1 = g_strdup("[Error reading file]");
    }
    
    if (!g_file_get_contents(file2_path, &contents2, &length2, NULL)) {
        g_printerr("Failed to read file: %s\n", file2_path);
        contents2 = g_strdup("[Error reading file]");
    }

    // Populate buffers with full file contents first
    gtk_text_buffer_set_text(buffer1, contents1 ? contents1 : "", -1);
    gtk_text_buffer_set_text(buffer2, contents2 ? contents2 : "", -1);

    // Highlight differences using word-level comparison
    const gchar *p1 = contents1 ? contents1 : "";
    const gchar *p2 = contents2 ? contents2 : "";

    // Tokenize both files into words array (words are non-whitespace runs)
    GPtrArray *words1 = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *words2 = g_ptr_array_new_with_free_func(g_free);
    
    // Extract words from file1
    const gchar *q = p1;
    while (*q) {
        gunichar c = g_utf8_get_char(q);
        if (g_unichar_isspace(c)) { q = g_utf8_next_char(q); continue; }
        const gchar *start = q;
        while (*q) {
            gunichar n = g_utf8_get_char(q);
            if (g_unichar_isspace(n)) break;
            q = g_utf8_next_char(q);
        }
        gchar *w = g_strndup(start, q - start);
        g_ptr_array_add(words1, w);
    }
    
    // Extract words from file2
    const gchar *r = p2;
    while (*r) {
        gunichar c = g_utf8_get_char(r);
        if (g_unichar_isspace(c)) { r = g_utf8_next_char(r); continue; }
        const gchar *start = r;
        while (*r) {
            gunichar n = g_utf8_get_char(r);
            if (g_unichar_isspace(n)) break;
            r = g_utf8_next_char(r);
        }
        gchar *w = g_strndup(start, r - start);
        g_ptr_array_add(words2, w);
    }

    /* Use LCS-based approach to identify matched word positions */
    gint n1 = words1->len;
    gint n2 = words2->len;
    
    /* Create matching arrays: matched1[i] = j means word1[i] matches word2[j], or -1 if unmatched */
    gint *matched1 = g_new0(gint, n1);
    gint *matched2 = g_new0(gint, n2);
    
    for (gint i = 0; i < n1; i++) matched1[i] = -1;
    for (gint i = 0; i < n2; i++) matched2[i] = -1;
    
    /* Simple greedy matching: go through each word in file1 and find first matching word in file2 */
    for (gint i = 0; i < n1; i++) {
        gchar *w1 = g_ptr_array_index(words1, i);
        
        for (gint j = 0; j < n2; j++) {
            if (matched2[j] == -1) {  /* word2[j] not yet matched */
                gchar *w2 = g_ptr_array_index(words2, j);
                if (g_strcmp0(w1, w2) == 0) {
                    matched1[i] = j;
                    matched2[j] = i;
                    break;  /* Match this word and move to next word1 */
                }
            }
        }
    }

    /* Mark unmatched words in file1 as deleted (red) */
    gint offset1 = 0;
    q = p1;
    for (gint i = 0; i < n1; i++) {
        gchar *w1 = g_ptr_array_index(words1, i);
        
        /* Skip whitespace to find the word position */
        while (*q && g_unichar_isspace(g_utf8_get_char(q))) {
            q = g_utf8_next_char(q);
            offset1++;
        }
        
        gint wchars = g_utf8_strlen(w1, -1);
        
        /* If this word is not matched to any word in file2, mark as deleted */
        if (matched1[i] == -1 && wchars > 0) {
            GtkTextIter s, e;
            gtk_text_buffer_get_iter_at_offset(buffer1, &s, offset1);
            gtk_text_buffer_get_iter_at_offset(buffer1, &e, offset1 + wchars);
            gtk_text_buffer_apply_tag_by_name(buffer1, "diff-delete", &s, &e);
        }
        
        offset1 += wchars;
        q = g_utf8_offset_to_pointer(q, wchars);
    }

    /* Mark unmatched words in file2 as added (green) */
    gint offset2 = 0;
    r = p2;
    for (gint i = 0; i < n2; i++) {
        gchar *w2 = g_ptr_array_index(words2, i);
        
        /* Skip whitespace to find the word position */
        while (*r && g_unichar_isspace(g_utf8_get_char(r))) {
            r = g_utf8_next_char(r);
            offset2++;
        }
        
        gint wchars = g_utf8_strlen(w2, -1);
        
        /* If this word is not matched to any word in file1, mark as added */
        if (matched2[i] == -1 && wchars > 0) {
            GtkTextIter s, e;
            gtk_text_buffer_get_iter_at_offset(buffer2, &s, offset2);
            gtk_text_buffer_get_iter_at_offset(buffer2, &e, offset2 + wchars);
            gtk_text_buffer_apply_tag_by_name(buffer2, "diff-insert", &s, &e);
        }
        
        offset2 += wchars;
        r = g_utf8_offset_to_pointer(r, wchars);
    }

    g_free(matched1);
    g_free(matched2);

    g_ptr_array_free(words1, TRUE);
    g_ptr_array_free(words2, TRUE);

    g_free(contents1);
    g_free(contents2);

    /* Connect revert button signal */
    RevertData *revert_data = g_new(RevertData, 1);
    revert_data->diff_window = GTK_WINDOW(window);
    revert_data->latest_file_path = g_strdup(file2_path);  /* file2 is the latest */
    const char *orig_path = g_object_get_data(G_OBJECT(parent), "original-path");
    revert_data->original_file_path = g_strdup(orig_path ? orig_path : "");
    revert_data->parent_window = parent;
    
    /* Try to get the versions list from the parent window */
    revert_data->versions_list = NULL;
    revert_data->version_row = version_row;  /* Store the version row to remove from UI */
    if (parent) {
        GtkWidget *toplevel = GTK_WIDGET(parent);
        GtkWidget *versions_list_widget = g_object_get_data(G_OBJECT(toplevel), "versions-list");
        if (versions_list_widget && GTK_IS_LIST_BOX(versions_list_widget)) {
            revert_data->versions_list = GTK_LIST_BOX(versions_list_widget);
        }
    }

    g_signal_connect(revert_button, "clicked", G_CALLBACK(on_revert_button_clicked), revert_data);

    gtk_window_present(GTK_WINDOW(window));
}