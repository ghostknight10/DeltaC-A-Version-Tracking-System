#include <gtk/gtk.h>
#include "sidebar.h"
#include "context_menu.h"
#include <stdlib.h> // For _putenv_s on Windows
// Use a struct to hold application state instead of globals
typedef struct {
    gboolean is_dark_mode;
} AppData;

/* Helper struct and idle callback to set paned position after window shown */
typedef struct { GtkWidget *window; GtkWidget *paned; } PanedSetData;

static gboolean paned_set_cb(gpointer user_data) {
    PanedSetData *d = (PanedSetData *)user_data;
    if (!d) return G_SOURCE_REMOVE;
    if (!GTK_IS_WINDOW(d->window) || !GTK_IS_PANED(d->paned)) { g_free(d); return G_SOURCE_REMOVE; }
    // Use the modern, non-deprecated GTK4 function to get widget width.
    int w = gtk_widget_get_width(GTK_WIDGET(d->window));
    int pos = (int)(w * 0.40);
    gtk_paned_set_position(GTK_PANED(d->paned), pos);
    g_free(d);
    return G_SOURCE_REMOVE;
}
// Dark mode callback now uses the AppData struct
static void on_toggle_button_clicked(GtkButton *button, gpointer user_data) {
    AppData *data = (AppData *)user_data; // Get our data struct

    data->is_dark_mode = !data->is_dark_mode;
    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", data->is_dark_mode, NULL);
    
    if (data->is_dark_mode) {
        gtk_button_set_label(button, "Light Mode");
    } else {
        gtk_button_set_label(button, "Dark Mode");
    }
}

// This function builds the UI when the application is activated
static void on_activate(GApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *main_vbox;
    GtkWidget *header_box;
    GtkWidget *header_label;
    GtkWidget *toggle_button;
    GtkWidget *main_paned;
    
    // NEW sidebar and content widgets
    GtkWidget *sidebar;

    GtkCssProvider *cssProvider;
    GdkDisplay *display;
    // Create our data struct and initialize it
    AppData *data = g_new(AppData, 1);
    data->is_dark_mode = TRUE;
    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", data->is_dark_mode, NULL);
    
    // 1. Create Window (GTK4 style)
    //
    // FIX 1: Cast the GApplication* to a GtkApplication*
    //
    window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "deltaC");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    // Free our data struct when the window is destroyed
    g_signal_connect(window, "destroy", G_CALLBACK(g_free), data);

    // 2. Create Header
    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(header_box, "header");

    header_label = gtk_label_new("deltaC");
    gtk_widget_set_halign(header_label, GTK_ALIGN_START);
    gtk_widget_set_name(header_label, "header-label");

    toggle_button = gtk_button_new_with_label("Light Mode");
    // Pass our 'data' struct to the callback
    g_signal_connect(toggle_button, "clicked", G_CALLBACK(on_toggle_button_clicked), data);

    // GTK4: Use gtk_box_append and set expand/fill on the child
    gtk_widget_set_hexpand(header_label, TRUE);
    gtk_widget_set_halign(header_label, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(header_box), header_label);
    gtk_box_append(GTK_BOX(header_box), toggle_button);
    gtk_box_append(GTK_BOX(main_vbox), header_box);

    // 3. Create the resizable pane
    main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    // GTK4: Set vexpand/valign on the child
    gtk_widget_set_vexpand(main_paned, TRUE);
    gtk_widget_set_valign(main_paned, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(main_vbox), main_paned);

    // 4. Create and add the sidebar
    // This function must also be GTK4-friendly (as converted in previous steps)
    sidebar = create_sidebar(GTK_WINDOW(window));
    
    gtk_widget_set_margin_start(sidebar, 10);
    gtk_widget_set_margin_bottom(sidebar, 10);
    gtk_widget_set_margin_top(sidebar, 10);
    
    // GTK4: Use gtk_paned_set_start_child
    gtk_paned_set_start_child(GTK_PANED(main_paned), sidebar);
    gtk_widget_set_size_request(sidebar, 250, -1);

    // 5. Create the Right Pane (Versions list)
    GtkWidget *versions_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(versions_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *versions_list = gtk_list_box_new();
    gtk_widget_set_name(versions_list, "versions-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(versions_scrolled), versions_list);

    // Store the versions list on the window so sidebar can populate it
    g_object_set_data(G_OBJECT(window), "versions-list", versions_list);

    // GTK4: Use gtk_paned_set_end_child
    gtk_paned_set_end_child(GTK_PANED(main_paned), versions_scrolled);

    // 6. Add main_vbox to window
    // GTK4: Use gtk_window_set_child
    gtk_window_set_child(GTK_WINDOW(window), main_vbox);

    // 7. Load CSS (Updated for GTK4 syntax)
    // 7. Load CSS (Updated for GTK4 syntax)
    cssProvider = gtk_css_provider_new();
    const char *css_data =
        "* { font-size: 102%; }"
        "#header {"
        "   background: @fg_color;" /* FIX: Added @ */
        "   padding: 10px;"
        "}"
        "#header-label {"
        "   color: @bg_color;"      /* FIX: Added @ */
        "   font-size: 1.5em;"
        "}"
        "#big-label {"
        "   font-size: 1.5em;"
        "}"
        ".sidebar-button {"
        "   border-radius: 0;"
        "   padding: 8px;"
        "}"
        ".sidebar-button:hover {"
        "   background-image: none;"
        /* FIX: Added @ to colors */
        "   background: mix(@fg_color, @bg_color, 0.1);"
        "}"
        "#file-list-box row label {"
        "   font-size: 1em;"
        "   padding-top: 2px;"
        "   padding-bottom: 2px;"
        "}";

    //
    // FIX 2: Use the new, non-deprecated function
    //
    gtk_css_provider_load_from_string(cssProvider, css_data);

    // 8. Apply CSS (GTK4 style)
    display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(display,
                                               GTK_STYLE_PROVIDER(cssProvider),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(cssProvider); // Can unref immediately

    // 9. Show the window
    // GTK4: No gtk_widget_show_all()
    gtk_window_present(GTK_WINDOW(window));

    /* Set the paned position after the window is visible to make start child ~40% */
    typedef struct { GtkWidget *window; GtkWidget *paned; } PanedSetData;
    PanedSetData *psd = g_new0(PanedSetData, 1);
    psd->window = window;
    psd->paned = main_paned;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, paned_set_cb, psd, NULL);
}

// The new main function just sets up and runs the GtkApplication
int main(int argc, char **argv) {
    // Set the GSK_RENDERER environment variable to "cairo" for this process.
    // This is done to prevent screen flickering issues on some Windows systems
    // by forcing a software rendering backend. This must be done before
    // GTK is initialized (i.e., before g_application_run).
    // We use the secure _putenv_s on Windows.
    _putenv_s("GSK_RENDERER", "cairo");

    // 1. Create a new application
    GtkApplication *app = gtk_application_new(
        "com.example.gyatthub", // A unique ID
        G_APPLICATION_DEFAULT_FLAGS
    );

    // 2. Connect the "activate" signal to our UI-building function
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    // 3. Run the application
    int status = g_application_run(G_APPLICATION(app), argc, argv);

    // 4. Clean up
    g_object_unref(app);

    return status;
}