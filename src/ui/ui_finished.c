/*
 * ui_finished.c
 * Installation finished / failed states
 */
#include "neko_installer.h"
#include "ui_internal.h"
#include <string.h>

#define NEKO_ISSUE_URL "https://github.com/Neko-Void-Linux/Neko-Void/issues/new"

typedef struct {
    AppData *app;
    GtkWidget *status_label;
    gchar *reason_key;
} FailMessage;

/* Copy the whole on-screen installation log to the clipboard. */
static void copy_install_log(AppData *app, GtkWidget *status_label) {
    if (!app->console_text) return;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->console_text));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, text ? text : "", -1);
    g_free(text);

    if (status_label)
        gtk_label_set_text(GTK_LABEL(status_label),
                           get_loc("error_log_copied", app->current_lang));
}

/* Open the issue tracker in the default browser. */
static void open_issue_page(AppData *app) {
    GError *error = NULL;
    gtk_show_uri_on_window(GTK_WINDOW(app->window), NEKO_ISSUE_URL,
                           GDK_CURRENT_TIME, &error);
    if (error) {
        g_printerr("Could not open issue page: %s\n", error->message);
        g_error_free(error);
    }
}

static void on_error_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    FailMessage *fm = (FailMessage *)user_data;

    if (response_id == GTK_RESPONSE_APPLY) {
        copy_install_log(fm->app, fm->status_label);
    } else if (response_id == GTK_RESPONSE_HELP) {
        open_issue_page(fm->app);
    } else {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void set_ui_failed_safe(AppData *app, const char *reason_key) {
    int lang = app->current_lang;

    app->installing = FALSE;

    /* Re-enable every notebook page so the user can review and fix the
     * configuration before retrying (they were disabled during install). */
    {
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        for (gint i = 0; i < n; i++) {
            GtkWidget *pg = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
            gtk_widget_set_sensitive(pg, TRUE);
        }
    }
    gint cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    /* Refresh the Back/Next buttons for the current page. */
    on_page_changed(GTK_NOTEBOOK(app->notebook), NULL, (guint)cur, app);
    gtk_widget_set_sensitive(app->btn_back, cur > 0);

    /* Allow the user to retry the installation after fixing the problem.
     * The button turns red to signal the failure state. */
    gtk_button_set_label(GTK_BUTTON(app->btn_install), get_loc("error_retry_btn", lang));
    GtkStyleContext *ctx = gtk_widget_get_style_context(app->btn_install);
    gtk_style_context_remove_class(ctx, "suggested-action");
    gtk_style_context_add_class(ctx, "destructive-action");
    gtk_widget_set_sensitive(app->btn_install, TRUE);

    FailMessage *fm = g_new0(FailMessage, 1);
    fm->app = app;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_NONE,
        "%s", get_loc(reason_key, lang));
    gtk_window_set_title(GTK_WINDOW(dialog), get_loc("error_popup_title", lang));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "%s", get_loc("error_popup_body", lang));

    /* Show the report URL inside the dialog so it can be copied by hand. */
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gchar *link_markup = g_strdup_printf("<b>%s</b>\n<a href=\"%s\">%s</a>",
        get_loc("error_report_link", lang), NEKO_ISSUE_URL, NEKO_ISSUE_URL);
    GtkWidget *link_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(link_label), link_markup);
    gtk_label_set_selectable(GTK_LABEL(link_label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(link_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(link_label), 0.0);
    gtk_box_pack_start(GTK_BOX(area), link_label, FALSE, FALSE, 0);

    fm->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(fm->status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(area), fm->status_label, FALSE, FALSE, 0);

    gtk_dialog_add_button(GTK_DIALOG(dialog), get_loc("error_copy_log", lang), GTK_RESPONSE_APPLY);
    gtk_dialog_add_button(GTK_DIALOG(dialog), get_loc("error_send_log", lang), GTK_RESPONSE_HELP);
    gtk_dialog_add_button(GTK_DIALOG(dialog), get_loc("error_close", lang), GTK_RESPONSE_CLOSE);

    g_signal_connect(dialog, "response", G_CALLBACK(on_error_dialog_response), fm);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(link_markup);
    g_free(fm);
}

static gboolean set_ui_failed_wrapper(gpointer data) {
    FailMessage *fm = (FailMessage *)data;
    set_ui_failed_safe(fm->app, fm->reason_key);
    g_free(fm->reason_key);
    g_free(fm);
    return FALSE;
}

void set_ui_failed(AppData *app, const char *reason_key) {
    FailMessage *fm = g_new0(FailMessage, 1);
    fm->app = app;
    fm->reason_key = g_strdup(reason_key);
    g_idle_add(set_ui_failed_wrapper, fm);
}

static void set_ui_finished_safe(gpointer data) {
    AppData *app = (AppData *)data;

    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);

    /* Disable every notebook page EXCEPT the install page (last page)
     * so the user can still scroll through the log after installation. */
    {
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        for (gint i = 0; i < n - 1; i++) {
            GtkWidget *pg = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
            gtk_widget_set_sensitive(pg, FALSE);
        }
    }

    gtk_button_set_label(GTK_BUTTON(app->btn_install), get_loc("reboot_btn", app->current_lang));
    /* Success: the button turns green. */
    {
        GtkStyleContext *ctx = gtk_widget_get_style_context(app->btn_install);
        gtk_style_context_remove_class(ctx, "destructive-action");
        gtk_style_context_add_class(ctx, "suggested-action");
    }
    g_signal_handlers_disconnect_by_func(app->btn_install, G_CALLBACK(start_installation), app);
    g_signal_connect(app->btn_install, "clicked", G_CALLBACK(on_reboot_clicked), app);
    gtk_widget_set_sensitive(app->btn_install, TRUE);

    GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        get_loc("success_title", app->current_lang));

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog),
        get_loc("success_body", app->current_lang));

    GtkWidget *btn_reboot_popup = gtk_dialog_add_button(GTK_DIALOG(success_dialog), get_loc("success_reboot", app->current_lang), GTK_RESPONSE_ACCEPT);
    GtkStyleContext *context = gtk_widget_get_style_context(btn_reboot_popup);
    gtk_style_context_add_class(context, "suggested-action");
    /* The title-bar X / Escape only closes the dialog (no reboot). */
    g_signal_connect(success_dialog, "response", G_CALLBACK(on_popup_reboot), NULL);
    gtk_widget_show_all(success_dialog);
    // return FALSE; // void function signature mismatch if we return FALSE here? No, g_idle_add expects FALSE
}

// wait wrapper for g_idle_add
static gboolean set_ui_finished_wrapper(gpointer data) {
    set_ui_finished_safe(data);
    return FALSE;
}

void set_ui_finished(AppData *app) { g_idle_add(set_ui_finished_wrapper, app); }