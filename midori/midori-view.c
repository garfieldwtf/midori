/*
 Copyright (C) 2007-2009 Christian Dywan <christian@twotoasts.de>
 Copyright (C) 2009 Jean-François Guchens <zcx000@gmail.com>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#if HAVE_CONFIG_H
    #include <config.h>
#endif

#include "midori-view.h"
#include "midori-stock.h"

#include "compat.h"
#include "marshal.h"
#include "sokoke.h"

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <webkit/webkit.h>

/* This is unstable API, so we need to declare it */
gchar*
webkit_web_view_get_selected_text (WebKitWebView* web_view);
/* This is public API since WebKitGTK+ 1.1.6 */
#if !WEBKIT_CHECK_VERSION (1, 1, 6)
void
webkit_web_frame_print (WebKitWebFrame* web_frame);
#endif

GdkPixbuf*
midori_search_action_get_icon (KatzeNet*  net,
                               KatzeItem* item,
                               GtkWidget* widget);

static void
midori_view_construct_web_view (MidoriView* view);

struct _MidoriView
{
    GtkScrolledWindow parent_instance;

    gchar* uri;
    gchar* title;
    gchar* mime_type;
    GdkPixbuf* icon;
    gdouble progress;
    MidoriLoadStatus load_status;
    gchar* statusbar_text;
    gchar* link_uri;
    gboolean has_selection;
    gchar* selected_text;
    MidoriWebSettings* settings;
    GtkWidget* web_view;
    KatzeArray* news_feeds;

    gboolean speed_dial_in_new_tabs;
    gchar* download_manager;
    gchar* news_aggregator;
    gboolean middle_click_opens_selection;
    gboolean open_tabs_in_the_background;
    gboolean close_buttons_on_tabs;
    MidoriNewPage open_new_pages_in;
    gboolean find_while_typing;

    GtkWidget* menu_item;
    GtkWidget* tab_label;
    GtkWidget* tab_icon;
    GtkWidget* tab_title;
    GtkWidget* tab_close;
    KatzeItem* item;

    KatzeNet* net;
};

struct _MidoriViewClass
{
    GtkScrolledWindowClass parent_class;
};

G_DEFINE_TYPE (MidoriView, midori_view, GTK_TYPE_SCROLLED_WINDOW)

GType
midori_load_status_get_type (void)
{
    static GType type = 0;
    static const GEnumValue values[] = {
     { MIDORI_LOAD_PROVISIONAL, "MIDORI_LOAD_PROVISIONAL", "Load Provisional" },
     { MIDORI_LOAD_COMMITTED, "MIDORI_LOAD_COMMITTED", "Load Committed" },
     { MIDORI_LOAD_FINISHED, "MIDORI_LOAD_FINISHED", "Load Finished" },
     { 0, NULL, NULL }
    };

    if (type)
        return type;

    type = g_enum_register_static ("MidoriLoadStatus", values);
    return type;
}

GType
midori_new_view_get_type (void)
{
    static GType type = 0;
    if (!type)
    {
        static const GEnumValue values[] = {
         { MIDORI_NEW_VIEW_TAB, "MIDORI_NEW_VIEW_TAB", "New view in a tab" },
         { MIDORI_NEW_VIEW_BACKGROUND, "MIDORI_NEW_VIEW_BACKGROUND",
             "New view in a background tab" },
         { MIDORI_NEW_VIEW_WINDOW, "MIDORI_NEW_VIEW_WINDOW",
             "New view in a window" },
         { 0, NULL, NULL }
        };
        type = g_enum_register_static ("MidoriNewView", values);
    }
    return type;
}

enum
{
    PROP_0,

    PROP_URI,
    PROP_TITLE,
    PROP_MIME_TYPE,
    PROP_ICON,
    PROP_LOAD_STATUS,
    PROP_PROGRESS,
    PROP_ZOOM_LEVEL,
    PROP_NEWS_FEEDS,
    PROP_STATUSBAR_TEXT,
    PROP_SETTINGS,
    PROP_NET
};

enum {
    ACTIVATE_ACTION,
    CONSOLE_MESSAGE,
    CONTEXT_READY,
    ATTACH_INSPECTOR,
    NEW_TAB,
    NEW_WINDOW,
    NEW_VIEW,
    DOWNLOAD_REQUESTED,
    SEARCH_TEXT,
    ADD_BOOKMARK,
    SAVE_AS,
    ADD_SPEED_DIAL,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
midori_view_finalize (GObject* object);

static void
midori_view_set_property (GObject*      object,
                          guint         prop_id,
                          const GValue* value,
                          GParamSpec*   pspec);

static void
midori_view_get_property (GObject*    object,
                          guint       prop_id,
                          GValue*     value,
                          GParamSpec* pspec);

static void
midori_view_settings_notify_cb (MidoriWebSettings* settings,
                                GParamSpec*        pspec,
                                MidoriView*        view);

static void
midori_view_speed_dial_get_thumb (GtkWidget*   web_view,
                                 const gchar* message,
                                 MidoriView*  view);

static void
midori_view_speed_dial_save (GtkWidget*   web_view,
                             const gchar* message);


static void
midori_view_class_init (MidoriViewClass* class)
{
    GObjectClass* gobject_class;
    GParamFlags flags;

    signals[ACTIVATE_ACTION] = g_signal_new (
        "activate-action",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1,
        G_TYPE_STRING);

    signals[CONSOLE_MESSAGE] = g_signal_new (
        "console-message",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        midori_cclosure_marshal_VOID__STRING_INT_STRING,
        G_TYPE_NONE, 3,
        G_TYPE_STRING,
        G_TYPE_INT,
        G_TYPE_STRING);

    signals[CONTEXT_READY] = g_signal_new (
        "context-ready",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

    signals[ATTACH_INSPECTOR] = g_signal_new (
        "attach-inspector",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1,
        GTK_TYPE_WIDGET);

    signals[NEW_TAB] = g_signal_new (
        "new-tab",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        midori_cclosure_marshal_VOID__STRING_BOOLEAN,
        G_TYPE_NONE, 2,
        G_TYPE_STRING,
        G_TYPE_BOOLEAN);

    signals[NEW_WINDOW] = g_signal_new (
        "new-window",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1,
        G_TYPE_STRING);

    /**
     * MidoriView::new-view:
     * @view: the object on which the signal is emitted
     * @new_view: a newly created view
     * @where: where to open the view
     *
     * Emitted when a new view is created. The value of
     * @where determines where to open the view according
     * to how it was opened and user preferences.
     *
     * Since: 0.1.2
     */
    signals[NEW_VIEW] = g_signal_new (
        "new-view",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        midori_cclosure_marshal_VOID__OBJECT_ENUM,
        G_TYPE_NONE, 2,
        MIDORI_TYPE_VIEW,
        MIDORI_TYPE_NEW_VIEW);

    /**
     * MidoriView::download-requested:
     * @view: the object on which the signal is emitted
     * @download: a new download
     *
     * Emitted when a new download is requested, if a
     * file cannot be displayed or a download was started
     * from the context menu.
     *
     * If the download should be accepted, a callback
     * has to return %TRUE, and the download will also
     * be started automatically.
     *
     * Note: This requires WebKitGTK 1.1.3.
     *
     * Return value: %TRUE if the download was handled
     *
     * Since: 0.1.5
     */
    signals[DOWNLOAD_REQUESTED] = g_signal_new (
        "download-requested",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        g_signal_accumulator_true_handled,
        NULL,
        midori_cclosure_marshal_BOOLEAN__OBJECT,
        G_TYPE_BOOLEAN, 1,
        G_TYPE_OBJECT);

    /**
     * MidoriView::search-text:
     * @view: the object on which the signal is emitted
     * @found: whether the search was successful
     * @typing: whether the search was initiated by typing
     *
     * Emitted when a search is performed. Either manually
     * invoked or automatically by typing. The value of typing
     * is actually the text the user typed.
     *
     * Note that in 0.1.3 the argument @typing was introduced.
     */
    signals[SEARCH_TEXT] = g_signal_new (
        "search-text",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST),
        0,
        0,
        NULL,
        midori_cclosure_marshal_VOID__BOOLEAN_STRING,
        G_TYPE_NONE, 2,
        G_TYPE_BOOLEAN,
        G_TYPE_STRING);

    signals[ADD_BOOKMARK] = g_signal_new (
        "add-bookmark",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1,
        G_TYPE_STRING);

    signals[SAVE_AS] = g_signal_new (
        "save-as",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1,
        G_TYPE_STRING);

    /**
     * MidoriView::add-speed-dial:
     * @view: the object on which the signal is emitted
     * @uri: the URI to add to the speed dial
     *
     * Emitted when an URI is added to the spee dial page.
     *
     * Since: 0.1.7
     */
    signals[ADD_SPEED_DIAL] = g_signal_new (
        "add-speed-dial",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        0,
        NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE, 1,
        G_TYPE_STRING);

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = midori_view_finalize;
    gobject_class->set_property = midori_view_set_property;
    gobject_class->get_property = midori_view_get_property;

    flags = G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS;

    g_object_class_install_property (gobject_class,
                                     PROP_URI,
                                     g_param_spec_string (
                                     "uri",
                                     "Uri",
                                     "The URI of the currently loaded page",
                                     "about:blank",
                                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_TITLE,
                                     g_param_spec_string (
                                     "title",
                                     "Title",
                                     "The title of the currently loaded page",
                                     NULL,
                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
    * MidoriView:mime-type:
    *
    * The MIME type of the currently loaded page.
    *
    * Since: 0.1.2
    */
    g_object_class_install_property (gobject_class,
                                     PROP_MIME_TYPE,
                                     g_param_spec_string (
                                     "mime-type",
                                     "MIME Type",
                                     "The MIME type of the currently loaded page",
                                     "text/html",
                                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_ICON,
                                     g_param_spec_object (
                                     "icon",
                                     "Icon",
                                     "The icon of the view",
                                     GDK_TYPE_PIXBUF,
                                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_LOAD_STATUS,
                                     g_param_spec_enum (
                                     "load-status",
                                     "Load Status",
                                     "The current loading status",
                                     MIDORI_TYPE_LOAD_STATUS,
                                     MIDORI_LOAD_FINISHED,
                                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_PROGRESS,
                                     g_param_spec_double (
                                     "progress",
                                     "Progress",
                                     "The current loading progress",
                                     0.0, 1.0, 0.0,
                                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_ZOOM_LEVEL,
                                     g_param_spec_float (
                                     "zoom-level",
                                     "Zoom Level",
                                     "The current zoom level",
                                     G_MINFLOAT,
                                     G_MAXFLOAT,
                                     1.0f,
                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
    * MidoriView:news-feeds:
    *
    * The news feeds advertised by the currently loaded page.
    *
    * Since: 0.1.7
    */
    g_object_class_install_property (gobject_class,
                                     PROP_NEWS_FEEDS,
                                     g_param_spec_object (
                                     "news-feeds",
                                     "News Feeds",
                                     "The list of available news feeds",
                                     KATZE_TYPE_ARRAY,
                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_STATUSBAR_TEXT,
                                     g_param_spec_string (
                                     "statusbar-text",
                                     "Statusbar Text",
                                     "The text displayed in the statusbar",
                                     "",
                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class,
                                     PROP_SETTINGS,
                                     g_param_spec_object (
                                     "settings",
                                     "Settings",
                                     "The associated settings",
                                     MIDORI_TYPE_WEB_SETTINGS,
                                     flags));

    g_object_class_install_property (gobject_class,
                                     PROP_NET,
                                     g_param_spec_object (
                                     "net",
                                     "Net",
                                     "The associated net",
                                     KATZE_TYPE_NET,
                                     flags));
}

static void
midori_view_update_title (MidoriView* view)
{
    /* If left-to-right text is combined with right-to-left text the default
       behaviour of Pango can result in awkwardly aligned text. For example
       "‪بستيان نوصر (hadess) | An era comes to an end - Midori" becomes
       "hadess) | An era comes to an end - Midori) بستيان نوصر". So to prevent
       this we insert an LRE character before the title which indicates that
       we want left-to-right but retains the direction of right-to-left text. */
    if (view->title && !g_str_has_prefix (view->title, "‪"))
    {
        gchar* new_title = g_strconcat ("‪", view->title, NULL);
        katze_assign (view->title, new_title);
    }
    #define title midori_view_get_display_title (view)
    if (view->tab_label)
    {
        /* If the title starts with the presumed name of the website, we
            ellipsize differently, to emphasize the subtitle */
        if (gtk_label_get_angle (GTK_LABEL (view->tab_title)) == 0.0)
        {
            SoupURI* uri = soup_uri_new (view->uri);
            const gchar* host = uri ? (uri->host ? uri->host : "") : "";
            const gchar* name = g_str_has_prefix (host, "www.") ? &host[4] : host;
            guint i = 0;
            while (name[i++])
                if (name[i] == '.')
                    break;
            if (!g_ascii_strncasecmp (title, name, i))
                gtk_label_set_ellipsize (GTK_LABEL (view->tab_title), PANGO_ELLIPSIZE_START);
            else
                gtk_label_set_ellipsize (GTK_LABEL (view->tab_title), PANGO_ELLIPSIZE_END);
            if (uri)
                soup_uri_free (uri);
        }
        gtk_label_set_text (GTK_LABEL (view->tab_title), title);
        gtk_widget_set_tooltip_text (view->tab_title, title);
    }
    if (view->menu_item)
        gtk_label_set_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (
                            view->menu_item))), title);
    if (view->item)
        katze_item_set_name (view->item, title);
    #undef title
}

static GdkPixbuf*
midori_view_mime_icon (GtkIconTheme* icon_theme,
                       const gchar*  format,
                       const gchar*  part1,
                       const gchar*  part2,
                       gchar**       name)
{
    GdkPixbuf* icon;

    *name = part2 ? g_strdup_printf (format, part1, part2)
        : g_strdup_printf (format, part1);
    if (!(icon = gtk_icon_theme_load_icon (icon_theme, *name, 16, 0, NULL)))
        g_free (*name);
    return icon ? g_object_ref (icon) : NULL;
}

static void
midori_view_update_icon (MidoriView* view,
                         GdkPixbuf*  icon)
{
    if (!icon)
    {
        GdkScreen* screen;
        GtkIconTheme* icon_theme;
        gchar** parts;
        gchar* icon_name;

        if ((screen = gtk_widget_get_screen (GTK_WIDGET (view))))
        {
            icon_theme = gtk_icon_theme_get_for_screen (screen);
            if ((parts = g_strsplit (view->mime_type, "/", 2)))
            {
                if (!(parts[0] && parts[1]))
                    katze_assign (parts, NULL);
            }
        }
        else
            parts = NULL;

        if (parts)
            icon = midori_view_mime_icon (icon_theme, "%s-%s",
                                          parts[0], parts[1], &icon_name);
        if (!icon && parts)
            icon = midori_view_mime_icon (icon_theme, "gnome-mime-%s-%s",
                                          parts[0], parts[1], &icon_name);
        if (!icon && parts)
            icon = midori_view_mime_icon (icon_theme, "%s-x-generic",
                                          parts[0], NULL, &icon_name);
        if (!icon && parts)
            icon = midori_view_mime_icon (icon_theme, "gnome-mime-%s-x-generic",
                                          parts[0], NULL, &icon_name);
        if (view->item)
            katze_item_set_icon (view->item, icon ? icon_name : NULL);
        if (!icon)
            icon = gtk_widget_render_icon (GTK_WIDGET (view),
                GTK_STOCK_FILE, GTK_ICON_SIZE_MENU, NULL);
    }
    else if (view->item)
        katze_item_set_icon (view->item, NULL);
    katze_object_assign (view->icon, icon);
    g_object_notify (G_OBJECT (view), "icon");

    if (view->tab_icon)
        katze_throbber_set_static_pixbuf (KATZE_THROBBER (view->tab_icon),
                                          view->icon);
    if (view->menu_item)
        gtk_image_menu_item_set_image (
            GTK_IMAGE_MENU_ITEM (view->menu_item),
                gtk_image_new_from_pixbuf (view->icon));
}

static void
midori_view_icon_cb (GdkPixbuf*  icon,
                     MidoriView* view)
{
    midori_view_update_icon (view, icon);
}

static void
_midori_web_view_load_icon (MidoriView* view)
{
    GdkPixbuf* pixbuf = katze_net_load_icon (view->net, view->uri,
        (KatzeNetIconCb)midori_view_icon_cb, NULL, view);

    midori_view_update_icon (view, pixbuf);
}

static void
midori_view_update_load_status (MidoriView*      view,
                                MidoriLoadStatus load_status)
{
    if (view->load_status == load_status)
        return;

    view->load_status = load_status;
    g_object_notify (G_OBJECT (view), "load-status");

    if (view->tab_icon)
        katze_throbber_set_animated (KATZE_THROBBER (view->tab_icon),
            view->load_status != MIDORI_LOAD_FINISHED);

    if (view->web_view && view->load_status == MIDORI_LOAD_COMMITTED)
        _midori_web_view_load_icon (view);
}

static void
webkit_web_view_load_started_cb (WebKitWebView*  web_view,
                                 WebKitWebFrame* web_frame,
                                 MidoriView*     view)
{
    g_object_freeze_notify (G_OBJECT (view));

    midori_view_update_load_status (view, MIDORI_LOAD_PROVISIONAL);
    view->progress = 0.0;
    g_object_notify (G_OBJECT (view), "progress");

    g_object_thaw_notify (G_OBJECT (view));
}

static void
webkit_web_view_load_committed_cb (WebKitWebView*  web_view,
                                   WebKitWebFrame* web_frame,
                                   MidoriView*     view)
{
    const gchar* uri;

    g_object_freeze_notify (G_OBJECT (view));

    uri = webkit_web_frame_get_uri (web_frame);
    g_return_if_fail (uri != NULL);
    katze_assign (view->uri, g_strdup (uri));
    if (view->item)
    {
        katze_item_set_uri (view->item, uri);
        katze_item_set_added (view->item, time (NULL));
    }
    g_object_notify (G_OBJECT (view), "uri");
    g_object_set (view, "title", NULL, NULL);

    midori_view_update_icon (view, NULL);

    midori_view_update_load_status (view, MIDORI_LOAD_COMMITTED);

    g_object_thaw_notify (G_OBJECT (view));
}

static void
webkit_web_view_progress_changed_cb (WebKitWebView* web_view,
                                     gint           progress,
                                     MidoriView*    view)
{
    view->progress = progress ? progress / 100.0 : 0.0;
    g_object_notify (G_OBJECT (view), "progress");
}

#if WEBKIT_CHECK_VERSION (1, 1, 6)
static gboolean
webkit_web_view_load_error_cb (WebKitWebView*  web_view,
                               WebKitWebFrame* web_frame,
                               const gchar*    uri,
                               GError*         error,
                               MidoriView*     view)
{
    const gchar* template_file = DATADIR "/midori/res/error.html";
    gchar* template;

    if (g_file_get_contents (template_file, &template, NULL, NULL))
    {
        SoupServer* res_server;
        guint port;
        gchar* res_root;
        gchar* stock_root;
        gchar* message;
        gchar* result;

        res_server = sokoke_get_res_server ();
        port = soup_server_get_port (res_server);
        res_root = g_strdup_printf ("http://localhost:%d/res", port);
        stock_root = g_strdup_printf ("http://localhost:%d/stock", port);

        message = g_strdup_printf (_("The page '%s' couldn't be loaded."), uri);
        result = sokoke_replace_variables (template,
            "{title}", _("Error"),
            "{message}", message,
            "{description}", error->message,
            "{tryagain}", _("Try again"),
            "{res}", res_root,
            "{stock}", stock_root,
            NULL);
        g_free (template);
        g_free (message);

        webkit_web_frame_load_alternate_string (web_frame,
            result, res_root, uri);
        g_free (res_root);
        g_free (stock_root);
        g_free (result);

        return TRUE;
    }

    return FALSE;
}
#else
static void
webkit_web_frame_load_done_cb (WebKitWebFrame* web_frame,
                               gboolean        success,
                               MidoriView*     view)
{
    gchar* title;
    gchar* data;

    if (!success)
    {
        /* i18n: The title of the 404 - Not found error page */
        title = g_strdup_printf (_("Not found - %s"), view->uri);
        katze_assign (view->title, title);
        data = g_strdup_printf (
            "<html><head><title>%s</title></head>"
            "<body><h1>%s</h1>"
            "<img src=\"file://" DATADIR "/midori/logo-shade.png\" "
            "style=\"position: absolute; right: 15px; bottom: 15px;\">"
            "<p />The page you were opening doesn't exist."
            "<p />Try to <a href=\"%s\">load the page again</a>, "
            "or move on to another page."
            "</body></html>",
            title, title, view->uri);
        webkit_web_view_load_html_string (
            WEBKIT_WEB_VIEW (view->web_view), data, view->uri);
        g_free (data);
    }

    midori_view_update_load_status (view, MIDORI_LOAD_FINISHED);
}
#endif

static void
webkit_web_view_load_finished_cb (WebKitWebView*  web_view,
                                  WebKitWebFrame* web_frame,
                                  MidoriView*     view)
{
    g_object_freeze_notify (G_OBJECT (view));

    view->progress = 1.0;
    g_object_notify (G_OBJECT (view), "progress");
    midori_view_update_load_status (view, MIDORI_LOAD_FINISHED);

    if (view->news_aggregator && *view->news_aggregator)
    {
        JSContextRef js_context = webkit_web_frame_get_global_context (web_frame);
        /* This snippet joins the available news feeds into a string like this:
           URI1|title1,URI2|title2
           FIXME: Ensure separators contained in the string can't break it */
        gchar* value = sokoke_js_script_eval (js_context,
        "function feeds (l) { var f = new Array (); for (i in l) "
        "{ var t = l[i].type; "
        "if (t && (t.indexOf ('rss') != -1 || t.indexOf ('atom') != -1)) "
        "f.push (l[i].href + '|' + l[i].title); } return f; }"
        "feeds (document.getElementsByTagName ('link'))", NULL);
        gchar** items = g_strsplit (value, ",", 0);
        gchar** iter;

        katze_array_clear (view->news_feeds);
        for (iter = items; iter && *iter; iter++)
        {
            gchar** parts = g_strsplit (*iter, "|", 2);
            KatzeItem* item = g_object_new (KATZE_TYPE_ITEM,
                "uri", parts ? *parts : "",
                "name", parts && *parts ? parts[1] : NULL,
                NULL);
            katze_array_add_item (view->news_feeds, item);
            g_object_unref (item);
            g_strfreev (parts);
        }
        g_strfreev (items);
        g_object_set_data (G_OBJECT (view), "news-feeds",
                           value && *value ? (void*)1 : (void*)0);
        g_free (value);
        /* Ensure load-status is notified again, whether it changed or not */
        g_object_notify (G_OBJECT (view), "load-status");
    }

    g_object_thaw_notify (G_OBJECT (view));
}

#if WEBKIT_CHECK_VERSION (1, 1, 4)
static void
webkit_web_view_notify_uri_cb (WebKitWebView* web_view,
                               GParamSpec*    pspec,
                               MidoriView*    view)
{
    g_object_get (web_view, "uri", &view->uri, NULL);
    g_object_notify (G_OBJECT (view), "uri");
}

static void
webkit_web_view_notify_title_cb (WebKitWebView* web_view,
                                 GParamSpec*    pspec,
                                 MidoriView*    view)
{
    g_object_get (web_view, "title", &view->title, NULL);
    midori_view_update_title (view);
    g_object_notify (G_OBJECT (view), "title");
}
#else
static void
webkit_web_view_title_changed_cb (WebKitWebView*  web_view,
                                  WebKitWebFrame* web_frame,
                                  const gchar*    title,
                                  MidoriView*     view)
{
    g_object_set (view, "title", title, NULL);
}
#endif

static void
webkit_web_view_statusbar_text_changed_cb (WebKitWebView* web_view,
                                           const gchar*   text,
                                           MidoriView*    view)
{
    g_object_set (G_OBJECT (view), "statusbar-text", text, NULL);
}

static void
webkit_web_view_hovering_over_link_cb (WebKitWebView* web_view,
                                       const gchar*   tooltip,
                                       const gchar*   link_uri,
                                       MidoriView*    view)
{
    katze_assign (view->link_uri, g_strdup (link_uri));
    g_object_set (G_OBJECT (view), "statusbar-text", link_uri, NULL);
}

#define MIDORI_KEYS_MODIFIER_MASK (GDK_SHIFT_MASK | GDK_CONTROL_MASK \
    | GDK_MOD1_MASK | GDK_META_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK )

static gboolean
gtk_widget_button_press_event_cb (WebKitWebView*  web_view,
                                  GdkEventButton* event,
                                  MidoriView*     view)
{
    GtkClipboard* clipboard;
    gchar* uri;
    gchar* new_uri;
    const gchar* link_uri;
    gboolean background;

    event->state = event->state & MIDORI_KEYS_MODIFIER_MASK;
    link_uri = midori_view_get_link_uri (MIDORI_VIEW (view));

    switch (event->button)
    {
    case 1:
        if (!link_uri)
            return FALSE;
        #if HAVE_OSX
        /* FIXME: Test for Command key */
        if (0)
        #else
        if (event->state & GDK_CONTROL_MASK)
        #endif
        {
            /* Open link in new tab */
            background = view->open_tabs_in_the_background;
            if (event->state & GDK_SHIFT_MASK)
                background = !background;
            g_signal_emit (view, signals[NEW_TAB], 0, link_uri, background);
            return TRUE;
        }
        else if (event->state & GDK_SHIFT_MASK)
        {
            /* Open link in new window */
            g_signal_emit (view, signals[NEW_WINDOW], 0, link_uri);
            return TRUE;
        }
        else if (event->state & GDK_MOD1_MASK)
        {
            /* Open link in new tab */
            background = view->open_tabs_in_the_background;
            if (event->state & GDK_CONTROL_MASK)
                background = !background;
            g_signal_emit (view, signals[NEW_TAB], 0, link_uri, background);
            return TRUE;
        }
        break;
    case 2:
        if (link_uri)
        {
            /* Open link in new tab */
            background = view->open_tabs_in_the_background;
            if (event->state & GDK_CONTROL_MASK)
                background = !background;
            g_signal_emit (view, signals[NEW_TAB], 0, link_uri, background);
            return TRUE;
        }
        else if (event->state & GDK_CONTROL_MASK)
        {
            midori_view_set_zoom_level (MIDORI_VIEW (view), 1.0);
            return FALSE; /* Allow Ctrl + Middle click */
        }
        else if (view->middle_click_opens_selection)
        {
            guint i = 0;
            clipboard = gtk_clipboard_get_for_display (
                gtk_widget_get_display (GTK_WIDGET (view)),
                GDK_SELECTION_PRIMARY);
            uri = gtk_clipboard_wait_for_text (clipboard);
            if (uri && strchr (uri, '.'))
            {
                while (uri[i++] != '\0')
                    if (uri[i] == '\n' || uri[i] == '\r')
                        uri[i] = ' ';
                new_uri = sokoke_magic_uri (g_strstrip (uri), NULL);
                if (event->state & GDK_CONTROL_MASK)
                {
                    background = view->open_tabs_in_the_background;
                    if (event->state & GDK_CONTROL_MASK)
                        background = !background;
                    g_signal_emit (view, signals[NEW_TAB], 0, new_uri, background);
                }
                else
                {
                    midori_view_set_uri (MIDORI_VIEW (view), new_uri);
                    gtk_widget_grab_focus (GTK_WIDGET (view));
                }
                g_free (new_uri);
                return TRUE;
            }
            g_free (uri);
        }
        break;
    case 8:
        midori_view_go_back (view);
        return TRUE;
    case 9:
        midori_view_go_forward (view);
        return TRUE;
    /*
     * On some fancier mice the scroll wheel can be used to scroll horizontally.
     * A middle click usually registers both a middle click (2) and a
     * horizontal scroll (11 or 12).
     * We catch horizontal scrolls and ignore them to prevent middle clicks from
     * accidentally being interpreted as first button clicks.
     */
    case 11:
        return TRUE;
    case 12:
        return TRUE;
    }

    return FALSE;
}

static gboolean
gtk_widget_key_press_event_cb (WebKitWebView* web_view,
                               GdkEventKey*   event,
                               MidoriView*    view)
{
    guint character = gdk_unicode_to_keyval (event->keyval);
    /* Skip control characters */
    if (character == (event->keyval | 0x01000000))
        return FALSE;

    if (character == '.' || character == '/')
        character = '\0';
    else if (!view->find_while_typing)
        return FALSE;

    if (!webkit_web_view_can_cut_clipboard (web_view)
        && !webkit_web_view_can_paste_clipboard (web_view))
    {
        gchar* text = character ? g_strdup_printf ("%c", character) : g_strdup ("");

        g_signal_emit (view, signals[SEARCH_TEXT], 0, TRUE, text);
        g_free (text);
    }

    return FALSE;
}

static gboolean
gtk_widget_scroll_event_cb (WebKitWebView*  web_view,
                            GdkEventScroll* event,
                            MidoriView*     view)
{
    event->state = event->state & MIDORI_KEYS_MODIFIER_MASK;

    if (event->state & GDK_CONTROL_MASK)
    {
        if (event->direction == GDK_SCROLL_DOWN)
            midori_view_set_zoom_level (view,
                midori_view_get_zoom_level (view) - 0.25f);
        else if(event->direction == GDK_SCROLL_UP)
            midori_view_set_zoom_level (view,
                midori_view_get_zoom_level (view) + 0.25f);
        return TRUE;
    }
    else
        return FALSE;
}

static void
midori_web_view_menu_new_tab_activate_cb (GtkWidget*  widget,
                                          MidoriView* view)
{
    gchar* uri = view->link_uri;

    if (!uri)
        uri = (gchar*)g_object_get_data (G_OBJECT (widget), "uri");
    g_signal_emit (view, signals[NEW_TAB], 0, uri,
        view->open_tabs_in_the_background);
}

static void
midori_web_view_menu_action_add_speed_dial_cb (GtkWidget*  widget,
                                              MidoriView* view)
{
    g_signal_emit (view, signals[ADD_SPEED_DIAL], 0, view->link_uri);
}

static void
midori_web_view_menu_search_web_activate_cb (GtkWidget*  widget,
                                             MidoriView* view)
{
    gchar* search;
    gchar* uri;

    if ((search = g_object_get_data (G_OBJECT (widget), "search")))
        search = g_strdup (search);
    else
        g_object_get (view->settings, "location-entry-search",
                      &search, NULL);
    uri = sokoke_search_uri (search, view->selected_text);
    g_free (search);

    g_signal_emit (view, signals[NEW_TAB], 0, uri,
        view->open_tabs_in_the_background);

    g_free (uri);
}

#if !WEBKIT_CHECK_VERSION (1, 1, 3)
static void
midori_web_view_menu_save_as_activate_cb (GtkWidget*  widget,
                                          MidoriView* view)
{
    g_signal_emit (view, signals[SAVE_AS], 0, view->link_uri);
}
#endif

static void
midori_web_view_menu_download_activate_cb (GtkWidget*  widget,
                                           MidoriView* view)
{
    sokoke_spawn_program (view->download_manager, view->link_uri, TRUE);
}

static void
midori_web_view_menu_add_bookmark_activate_cb (GtkWidget*  widget,
                                               MidoriView* view)
{
    g_signal_emit (view, signals[ADD_BOOKMARK], 0, view->link_uri);
}

static void
midori_web_view_menu_action_activate_cb (GtkWidget*  widget,
                                         MidoriView* view)
{
    const gchar* action = g_object_get_data (G_OBJECT (widget), "action");
    g_signal_emit (view, signals[ACTIVATE_ACTION], 0, action);
}

static void
webkit_web_view_populate_popup_cb (WebKitWebView* web_view,
                                   GtkWidget*     menu,
                                   MidoriView*    view)
{
    GtkWidget* menuitem;
    GtkWidget* icon;
    gchar* stock_id;
    GdkScreen* screen;
    GtkIconTheme* icon_theme;
    GList* items;
    gboolean has_selection;
    GtkWidget* label;

    has_selection = midori_view_has_selection (view);

    /* Unfortunately inspecting the menu is the only way to
       determine that the mouse is over a text area or selection. */
    items = gtk_container_get_children (GTK_CONTAINER (menu));
    menuitem = (GtkWidget*)g_list_nth_data (items, 0);
    g_list_free (items);
    if (GTK_IS_IMAGE_MENU_ITEM (menuitem))
    {
        icon = gtk_image_menu_item_get_image (GTK_IMAGE_MENU_ITEM (menuitem));
        gtk_image_get_stock (GTK_IMAGE (icon), &stock_id, NULL);
        if (!strcmp (stock_id, GTK_STOCK_CUT))
            return;
        if (strcmp (stock_id, GTK_STOCK_FIND))
            has_selection = FALSE;
    }

    if (view->link_uri)
    {
        items = gtk_container_get_children (GTK_CONTAINER (menu));
        menuitem = (GtkWidget*)g_list_nth_data (items, 0);
        /* hack to localize menu item */
        label = gtk_bin_get_child (GTK_BIN (menuitem));
        gtk_label_set_label (GTK_LABEL (label), _("Open _Link"));
        menuitem = gtk_image_menu_item_new_with_mnemonic (
            _("Open Link in New _Tab"));
        screen = gtk_widget_get_screen (GTK_WIDGET (view));
        icon_theme = gtk_icon_theme_get_for_screen (screen);
        if (gtk_icon_theme_has_icon (icon_theme, STOCK_TAB_NEW))
        {
            icon = gtk_image_new_from_stock (STOCK_TAB_NEW, GTK_ICON_SIZE_MENU);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
        }
        gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menuitem, 1);
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_new_tab_activate_cb), view);
        gtk_widget_show (menuitem);
        g_list_free (items);
        items = gtk_container_get_children (GTK_CONTAINER (menu));
        menuitem = (GtkWidget*)g_list_nth_data (items, 2);
        /* hack to localize menu item */
        label = gtk_bin_get_child (GTK_BIN (menuitem));
        gtk_label_set_label (GTK_LABEL (label), _("Open Link in New _Window"));
        menuitem = (GtkWidget*)g_list_nth_data (items, 3);
        g_list_free (items);
        #if WEBKIT_CHECK_VERSION (1, 1, 3)
        /* hack to localize menu item */
        label = gtk_bin_get_child (GTK_BIN (menuitem));
        gtk_label_set_label (GTK_LABEL (label), _("_Download Link destination"));
        #else
        /* hack to disable non-functional Download File
           FIXME: Make sure this really is the right menu item */
        gtk_widget_hide (menuitem);
        menuitem = gtk_image_menu_item_new_with_mnemonic (
            _("_Save Link destination"));
        gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menuitem, 3);
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_save_as_activate_cb), view);
        gtk_widget_show (menuitem);
        #endif
        if (view->download_manager && *view->download_manager)
        {
            menuitem = gtk_image_menu_item_new_with_mnemonic (
                _("Download with Download _Manager"));
            icon = gtk_image_new_from_stock (GTK_STOCK_SAVE_AS,
                                             GTK_ICON_SIZE_MENU);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
            gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menuitem, 4);
            g_signal_connect (menuitem, "activate",
                G_CALLBACK (midori_web_view_menu_download_activate_cb), view);
            gtk_widget_show (menuitem);
        }
        menuitem = gtk_image_menu_item_new_from_stock (STOCK_BOOKMARK_ADD, NULL);
        gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menuitem, 5);
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_add_bookmark_activate_cb), view);
        gtk_widget_show (menuitem);
    }

    if (!view->link_uri && has_selection)
    {
        GtkWidget* window;
        guint i;

        window = gtk_widget_get_toplevel (GTK_WIDGET (web_view));
        i = 0;
        if (katze_object_has_property (window, "search-engines"))
        {
            KatzeArray* search_engines;
            KatzeItem* item;
            GtkWidget* sub_menu = gtk_menu_new ();

            menuitem = gtk_image_menu_item_new_with_mnemonic (_("Search _with"));
            gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), sub_menu);
            gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menuitem, 1);
            gtk_widget_show (menuitem);

            search_engines = katze_object_get_object (window, "search-engines");
            while ((item = katze_array_get_nth_item (search_engines, i++)))
            {
                GdkPixbuf* pixbuf;
                menuitem = gtk_image_menu_item_new_with_mnemonic (katze_item_get_name (item));
                pixbuf = midori_search_action_get_icon (view->net, item,
                                                        GTK_WIDGET (web_view));
                icon = gtk_image_new_from_pixbuf (pixbuf);
                g_object_unref (pixbuf);
                gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
                gtk_menu_shell_insert (GTK_MENU_SHELL (sub_menu), menuitem, i - 1);
                g_object_set_data (G_OBJECT (menuitem), "search",
                                   (gchar*)katze_item_get_uri (item));
                g_signal_connect (menuitem, "activate",
                    G_CALLBACK (midori_web_view_menu_search_web_activate_cb), view);
                gtk_widget_show (menuitem);
            }
            g_object_unref (search_engines);
        }
        items = gtk_container_get_children (GTK_CONTAINER (menu));
        menuitem = (GtkWidget*)g_list_nth_data (items, 0);
        /* hack to localize menu item */
        label = gtk_bin_get_child (GTK_BIN (menuitem));
        gtk_label_set_label (GTK_LABEL (label), _("_Search the Web"));
        /* hack to implement Search the Web */
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_search_web_activate_cb), view);
        g_list_free (items);

        if (strchr (view->selected_text, '.')
            && !strchr (view->selected_text, ' '))
        {
            menuitem = gtk_image_menu_item_new_with_mnemonic (
                _("Open Address in New _Tab"));
            icon = gtk_image_new_from_stock (GTK_STOCK_JUMP_TO,
                                             GTK_ICON_SIZE_MENU);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
            gtk_menu_shell_insert (GTK_MENU_SHELL (menu), menuitem, -1);
            g_object_set_data (G_OBJECT (menuitem), "uri", view->selected_text);
            g_signal_connect (menuitem, "activate",
                G_CALLBACK (midori_web_view_menu_new_tab_activate_cb), view);
            gtk_widget_show (menuitem);
        }
        /* FIXME: view selection source */
    }

    if (!view->link_uri && !has_selection)
    {
        items = gtk_container_get_children (GTK_CONTAINER (menu));
        menuitem = (GtkWidget*)g_list_nth_data (items, 3);
        /* hack to localize menu item */
        if (GTK_IS_BIN (menuitem))
        {
            GtkStockItem stock_item;
            if (gtk_stock_lookup (GTK_STOCK_REFRESH, &stock_item))
            {
                label = gtk_bin_get_child (GTK_BIN (menuitem));
                gtk_label_set_label (GTK_LABEL (label), stock_item.label);
            }
        }
        g_list_free (items);
        menuitem = gtk_image_menu_item_new_with_mnemonic (_("Undo Close Tab"));
        icon = gtk_image_new_from_stock (GTK_STOCK_UNDELETE, GTK_ICON_SIZE_MENU);
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_object_set_data (G_OBJECT (menuitem), "action", "UndoTabClose");
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_action_activate_cb), view);
        /* FIXME: Make this sensitive only when there is a tab to undo */
        gtk_widget_show (menuitem);

        menuitem = gtk_separator_menu_item_new ();
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        gtk_widget_show (menuitem);

        menuitem = gtk_image_menu_item_new_from_stock (STOCK_BOOKMARK_ADD, NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_object_set_data (G_OBJECT (menuitem), "action", "BookmarkAdd");
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_action_activate_cb), view);
        gtk_widget_show (menuitem);

        if (view->speed_dial_in_new_tabs && !midori_view_is_blank (view))
        {
            menuitem = gtk_image_menu_item_new_with_mnemonic (_("Add to Speed _dial"));
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
            g_object_set_data (G_OBJECT (menuitem), "action", "AddSpeedDial");
            g_signal_connect (menuitem, "activate",
                G_CALLBACK (midori_web_view_menu_action_add_speed_dial_cb), view);
            gtk_widget_show (menuitem);
        }

        menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_SAVE_AS, NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_object_set_data (G_OBJECT (menuitem), "action", "SaveAs");
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_action_activate_cb), view);
        gtk_widget_show (menuitem);
        /* Currently views that don't support source, don't support
           saving either. If that changes, we need to think of something. */
        if (!midori_view_can_view_source (view))
            gtk_widget_set_sensitive (menuitem, FALSE);

        menuitem = gtk_image_menu_item_new_with_mnemonic (_("View _Source"));
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_object_set_data (G_OBJECT (menuitem), "action", "SourceView");
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_action_activate_cb), view);
        gtk_widget_show (menuitem);
        if (!midori_view_can_view_source (view))
            gtk_widget_set_sensitive (menuitem, FALSE);

        menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_PRINT, NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
        g_object_set_data (G_OBJECT (menuitem), "action", "Print");
        g_signal_connect (menuitem, "activate",
            G_CALLBACK (midori_web_view_menu_action_activate_cb), view);
        gtk_widget_show (menuitem);
    }
}

static gboolean
webkit_web_view_web_view_ready_cb (GtkWidget*  web_view,
                                   MidoriView* view)
{
    GtkWidget* new_view = gtk_widget_get_parent (web_view);
    MidoriNewView where = MIDORI_NEW_VIEW_TAB;
    if (view->open_new_pages_in == MIDORI_NEW_PAGE_TAB)
    {
        if (view->open_tabs_in_the_background)
            where = MIDORI_NEW_VIEW_BACKGROUND;
    }
    else if (view->open_new_pages_in == MIDORI_NEW_PAGE_WINDOW)
        where = MIDORI_NEW_VIEW_WINDOW;

    gtk_widget_show (new_view);
    if (view->open_new_pages_in == MIDORI_NEW_PAGE_CURRENT)
    {
        g_debug ("Opening all pages in current tab not implemented");
        g_signal_emit (view, signals[NEW_VIEW], 0, new_view, where);
    }
    else
        g_signal_emit (view, signals[NEW_VIEW], 0, new_view, where);

    return TRUE;
}

static GtkWidget*
webkit_web_view_create_web_view_cb (GtkWidget*      web_view,
                                    WebKitWebFrame* web_frame,
                                    MidoriView*     view)
{
    GtkWidget* new_view = g_object_new (MIDORI_TYPE_VIEW,
        "net", view->net,
        "settings", view->settings,
        NULL);
    midori_view_construct_web_view (MIDORI_VIEW (new_view));
    g_signal_connect (MIDORI_VIEW (new_view)->web_view, "web-view-ready",
      G_CALLBACK (webkit_web_view_web_view_ready_cb), view);
    return MIDORI_VIEW (new_view)->web_view;
}

static gboolean
webkit_web_view_mime_type_decision_cb (GtkWidget*               web_view,
                                       WebKitWebFrame*          web_frame,
                                       WebKitNetworkRequest*    request,
                                       const gchar*             mime_type,
                                       WebKitWebPolicyDecision* decision,
                                       MidoriView*              view)
{
    #if WEBKIT_CHECK_VERSION (1, 1, 3)
    GtkWidget* dialog;
    gchar* content_type;
    gchar* description;
    #if GTK_CHECK_VERSION (2, 14, 0)
    GIcon* icon;
    GtkWidget* image;
    #endif
    gchar* title;
    GdkScreen* screen;
    GtkIconTheme* icon_theme;
    gint response;
    #else
    gchar* uri;
    #endif

    if (web_frame != webkit_web_view_get_main_frame (WEBKIT_WEB_VIEW (web_view)))
        return FALSE;

    if (webkit_web_view_can_show_mime_type (WEBKIT_WEB_VIEW (web_view), mime_type))
    {
        katze_assign (view->mime_type, g_strdup (mime_type));
        midori_view_update_icon (view, NULL);
        g_object_notify (G_OBJECT (view), "mime-type");
        return FALSE;
    }

    #if WEBKIT_CHECK_VERSION (1, 1, 3)
    dialog = gtk_message_dialog_new (
        NULL, 0, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        _("Open or download file"));
    #if GLIB_CHECK_VERSION (2, 18, 0)
    content_type = g_content_type_from_mime_type (mime_type);
    #else
    content_type = g_strdup (mime_type);
    #endif
    description = g_content_type_get_description (content_type);
    #if GTK_CHECK_VERSION (2, 14, 0)
    icon = g_content_type_get_icon (content_type);
    image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
    g_object_unref (icon);
    gtk_widget_show (image);
    gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), image);
    #endif
    g_free (content_type);
    if (g_strrstr (description, mime_type))
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
        _("File Type: '%s'"), mime_type);
    else
       gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
       _("File Type: %s ('%s')"), description, mime_type);
    g_free (description);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), FALSE);
    /* i18n: A file open dialog title, ie. "Open http://fila.com/manual.tgz" */
    title = g_strdup_printf (_("Open %s"),
        webkit_network_request_get_uri (request));
    gtk_window_set_title (GTK_WINDOW (dialog), title);
    g_free (title);
    screen = gtk_widget_get_screen (dialog);
    if (screen)
    {
        icon_theme = gtk_icon_theme_get_for_screen (screen);
        if (gtk_icon_theme_has_icon (icon_theme, STOCK_TRANSFER))
            gtk_window_set_icon_name (GTK_WINDOW (dialog), STOCK_TRANSFER);
        else
            gtk_window_set_icon_name (GTK_WINDOW (dialog), GTK_STOCK_OPEN);
    }
    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
        GTK_STOCK_SAVE, 1,
        GTK_STOCK_CANCEL, 2,
        GTK_STOCK_OPEN, 3,
        NULL);
    response = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    g_object_set_data (G_OBJECT (view), "open-download", (gpointer)0);
    switch (response)
    {
        case 3:
            g_object_set_data (G_OBJECT (view), "open-download", (gpointer)1);
        case 1:
            webkit_web_policy_decision_download (decision);
            /* Apparently WebKit will continue loading which ends in an error.
               It's unclear whether it's a bug or we are doing something wrong. */
            webkit_web_view_stop_loading (WEBKIT_WEB_VIEW (view->web_view));
            return TRUE;
        case 2:
        default:
            /* Apparently WebKit will continue loading which ends in an error.
               It's unclear whether it's a bug or we are doing something wrong. */
            webkit_web_view_stop_loading (WEBKIT_WEB_VIEW (view->web_view));
            return FALSE;
    }
    #else
    katze_assign (view->mime_type, NULL);
    midori_view_update_icon (view, NULL);
    g_object_notify (G_OBJECT (view), "mime-type");

    uri = g_strdup_printf ("error:nodisplay %s",
        webkit_network_request_get_uri (request));
    midori_view_set_uri (view, uri);
    g_free (uri);

    return TRUE;
    #endif
}

#if WEBKIT_CHECK_VERSION (1, 1, 3)
static gboolean
webkit_web_view_download_requested_cb (GtkWidget*      web_view,
                                       WebKitDownload* download,
                                       MidoriView*     view)
{
    gboolean handled;
    g_object_set_data (G_OBJECT (download), "open-download",
        g_object_get_data (G_OBJECT (view), "open-download"));
    g_object_set_data (G_OBJECT (view), "open-download", (gpointer)0);
    g_signal_emit (view, signals[DOWNLOAD_REQUESTED], 0, download, &handled);
    return handled;
}
#endif

static gboolean
webkit_web_view_console_message_cb (GtkWidget*   web_view,
                                    const gchar* message,
                                    guint        line,
                                    const gchar* source_id,
                                    MidoriView*  view)
{
    if (!strncmp (message, "speed_dial-get-thumbnail", 22))
        midori_view_speed_dial_get_thumb (web_view, message, view);
    else if (!strncmp (message, "speed_dial-save", 13))
        midori_view_speed_dial_save (web_view, message);
    else
        g_signal_emit (view, signals[CONSOLE_MESSAGE], 0, message, line, source_id);
    return TRUE;
}

static void
webkit_web_view_window_object_cleared_cb (GtkWidget*      web_view,
                                          WebKitWebFrame* web_frame,
                                          JSContextRef    js_context,
                                          JSObjectRef     js_window,
                                          MidoriView*     view)
{
    g_signal_emit (view, signals[CONTEXT_READY], 0, js_context);
}

static void
midori_view_init (MidoriView* view)
{
    view->uri = NULL;
    view->title = NULL;
    view->mime_type = g_strdup ("text/html");
    view->icon = gtk_widget_render_icon (GTK_WIDGET (view), GTK_STOCK_FILE,
                                         GTK_ICON_SIZE_MENU, NULL);
    view->progress = 0.0;
    view->load_status = MIDORI_LOAD_FINISHED;
    view->statusbar_text = NULL;
    view->link_uri = NULL;
    view->selected_text = NULL;
    view->news_feeds = katze_array_new (KATZE_TYPE_ITEM);

    view->item = NULL;

    view->download_manager = NULL;
    view->news_aggregator = NULL;
    view->web_view = NULL;

    /* Adjustments are not created automatically */
    g_object_set (view, "hadjustment", NULL, "vadjustment", NULL, NULL);
}

static void
midori_view_finalize (GObject* object)
{
    MidoriView* view;

    view = MIDORI_VIEW (object);

    if (view->settings)
        g_signal_handlers_disconnect_by_func (view->settings,
            midori_view_settings_notify_cb, view);

    katze_assign (view->uri, NULL);
    katze_assign (view->title, NULL);
    katze_object_assign (view->icon, NULL);
    katze_assign (view->statusbar_text, NULL);
    katze_assign (view->link_uri, NULL);
    katze_assign (view->selected_text, NULL);
    katze_object_assign (view->news_feeds, NULL);

    katze_object_assign (view->settings, NULL);
    katze_object_assign (view->item, NULL);

    katze_assign (view->download_manager, NULL);
    katze_assign (view->news_aggregator, NULL);

    katze_object_assign (view->net, NULL);

    G_OBJECT_CLASS (midori_view_parent_class)->finalize (object);
}

static void
midori_view_set_property (GObject*      object,
                          guint         prop_id,
                          const GValue* value,
                          GParamSpec*   pspec)
{
    MidoriView* view;

    view = MIDORI_VIEW (object);

    switch (prop_id)
    {
    case PROP_TITLE:
        katze_assign (view->title, g_value_dup_string (value));
        midori_view_update_title (view);
        break;
    case PROP_ZOOM_LEVEL:
        midori_view_set_zoom_level (view, g_value_get_float (value));
        break;
    case PROP_STATUSBAR_TEXT:
        katze_assign (view->statusbar_text, g_value_dup_string (value));
        break;
    case PROP_SETTINGS:
        midori_view_set_settings (view, g_value_get_object (value));
        break;
    case PROP_NET:
        katze_object_assign (view->net, g_value_dup_object (value));
        if (!view->net)
            view->net = katze_net_new ();
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
midori_view_get_property (GObject*    object,
                          guint       prop_id,
                          GValue*     value,
                          GParamSpec* pspec)
{
    MidoriView* view = MIDORI_VIEW (object);

    switch (prop_id)
    {
    case PROP_URI:
        g_value_set_string (value, view->uri);
        break;
    case PROP_TITLE:
        g_value_set_string (value, view->title);
        break;
    case PROP_MIME_TYPE:
        g_value_set_string (value, view->mime_type);
        break;
    case PROP_ICON:
        g_value_set_object (value, view->icon);
        break;
    case PROP_PROGRESS:
        g_value_set_double (value, midori_view_get_progress (view));
        break;
    case PROP_LOAD_STATUS:
        g_value_set_enum (value, midori_view_get_load_status (view));
        break;
    case PROP_ZOOM_LEVEL:
        g_value_set_float (value, midori_view_get_zoom_level (view));
        break;
    case PROP_NEWS_FEEDS:
        g_value_set_object (value, view->news_feeds);
        break;
    case PROP_STATUSBAR_TEXT:
        g_value_set_string (value, view->statusbar_text);
        break;
    case PROP_SETTINGS:
        g_value_set_object (value, view->settings);
        break;
    case PROP_NET:
        g_value_set_object (value, view->net);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/**
 * midori_view_new:
 * @net: a #KatzeNet
 *
 * Creates a new view.
 *
 * Return value: a new #MidoriView
 **/
GtkWidget*
midori_view_new (KatzeNet* net)
{
    g_return_val_if_fail (!net || KATZE_IS_NET (net), NULL);

    return g_object_new (MIDORI_TYPE_VIEW, "net", net, NULL);
}

static void
_midori_view_update_settings (MidoriView* view)
{
    gboolean zoom_text_and_images;

    g_object_get (view->settings,
        "speed-dial-in-new-tabs", &view->speed_dial_in_new_tabs,
        "download-manager", &view->download_manager,
        "news-aggregator", &view->news_aggregator,
        "zoom-text-and-images", &zoom_text_and_images,
        "close-buttons-on-tabs", &view->close_buttons_on_tabs,
        "open-new-pages-in", &view->open_new_pages_in,
        "middle-click-opens-selection", &view->middle_click_opens_selection,
        "open-tabs-in-the-background", &view->open_tabs_in_the_background,
        "find-while-typing", &view->find_while_typing,
        NULL);

    if (view->web_view)
        g_object_set (view->web_view, "full-content-zoom",
                      zoom_text_and_images, NULL);
}

static void
midori_view_settings_notify_cb (MidoriWebSettings* settings,
                                GParamSpec*        pspec,
                                MidoriView*        view)
{
    const gchar* name;
    GValue value = { 0, };

    name = g_intern_string (g_param_spec_get_name (pspec));
    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (view->settings), name, &value);

    if (name == g_intern_string ("speed-dial-in-new-tabs"))
    {
        view->speed_dial_in_new_tabs = g_value_get_boolean (&value);
    }
    else if (name == g_intern_string ("download-manager"))
    {
        katze_assign (view->download_manager, g_value_dup_string (&value));
    }
    else if (name == g_intern_string ("news-aggregator"))
    {
        katze_assign (view->news_aggregator, g_value_dup_string (&value));
    }
    else if (name == g_intern_string ("zoom-text-and-images"))
    {
        if (view->web_view)
            g_object_set (view->web_view, "full-content-zoom",
                          g_value_get_boolean (&value), NULL);
    }
    else if (name == g_intern_string ("close-buttons-on-tabs"))
    {
        view->close_buttons_on_tabs = g_value_get_boolean (&value);
        sokoke_widget_set_visible (view->tab_close,
                                   view->close_buttons_on_tabs);
    }
    else if (name == g_intern_string ("open-new-pages-in"))
    {
        view->open_new_pages_in = g_value_get_enum (&value);
    }
    else if (name == g_intern_string ("middle-click-opens-selection"))
    {
        view->middle_click_opens_selection = g_value_get_boolean (&value);
    }
    else if (name == g_intern_string ("open-tabs-in-the-background"))
    {
        view->open_tabs_in_the_background = g_value_get_boolean (&value);
    }
    else if (name == g_intern_string ("find-while-typing"))
    {
        view->find_while_typing = g_value_get_boolean (&value);
    }

    g_value_unset (&value);
}

/**
 * midori_view_set_settings:
 * @view: a #MidoriView
 * @settings: a #MidoriWebSettings
 *
 * Assigns a settings instance to the view.
 **/
void
midori_view_set_settings (MidoriView*        view,
                          MidoriWebSettings* settings)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));
    g_return_if_fail (!settings || MIDORI_IS_WEB_SETTINGS (settings));

    if (view->settings == settings)
        return;

    if (view->settings)
        g_signal_handlers_disconnect_by_func (view->settings,
            midori_view_settings_notify_cb, view);

    katze_object_assign (view->settings, settings);
    if (settings)
    {
        g_object_ref (settings);
        if (view->web_view)
            g_object_set (view->web_view, "settings", settings, NULL);
        _midori_view_update_settings (view);
        g_signal_connect (settings, "notify",
            G_CALLBACK (midori_view_settings_notify_cb), view);
    }
    g_object_notify (G_OBJECT (view), "settings");
}

/**
 * midori_view_load_status:
 * @web_view: a #MidoriView
 *
 * Determines the current loading status of a view.
 *
 * Return value: the current #MidoriLoadStatus
 **/
MidoriLoadStatus
midori_view_get_load_status (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), MIDORI_LOAD_FINISHED);

    return view->load_status;
}

/**
 * midori_view_get_progress:
 * @view: a #MidoriView
 *
 * Retrieves the current loading progress as
 * a fraction between 0.0 and 1.0.
 *
 * Return value: the current loading progress
 **/
gdouble
midori_view_get_progress (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), 0.0);

    return view->progress;
}

static WebKitWebView*
webkit_web_inspector_inspect_web_view_cb (gpointer       inspector,
                                          WebKitWebView* web_view,
                                          MidoriView*    view)
{
    gchar* title;
    GtkWidget* window;
    GtkWidget* toplevel;
    GdkScreen* screen;
    gint width, height;
    GtkIconTheme* icon_theme;
    GdkPixbuf* icon;
    GdkPixbuf* gray_icon;
    GtkWidget* inspector_view;

    title = g_strdup_printf (_("Inspect page - %s"), "");
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (window), title);
    g_free (title);

    toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
    if (GTK_WIDGET_TOPLEVEL (toplevel))
    {
        gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (toplevel));
        screen = gtk_window_get_screen (GTK_WINDOW (toplevel));
        width = gdk_screen_get_width (screen) / 1.7;
        height = gdk_screen_get_height (screen) / 1.7;
        gtk_window_set_default_size (GTK_WINDOW (window), width, height);
    }

    /* Attempt to make a gray version of the icon on the fly */
    icon_theme = gtk_icon_theme_get_for_screen (
        gtk_widget_get_screen (GTK_WIDGET (view)));
    icon = gtk_icon_theme_load_icon (icon_theme, "midori", 32,
        GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
    if (icon)
    {
        gray_icon = gdk_pixbuf_copy (icon);
        if (gray_icon)
        {
            gdk_pixbuf_saturate_and_pixelate (gray_icon, gray_icon, 0.1f, FALSE);
            gtk_window_set_icon (GTK_WINDOW (window), gray_icon);
            g_object_unref (gray_icon);
        }
        g_object_unref (icon);
    }
    else
        gtk_window_set_icon_name (GTK_WINDOW (window), "midori");
    inspector_view = webkit_web_view_new ();
    gtk_container_add (GTK_CONTAINER (window), inspector_view);

    /* FIXME: Implement web inspector signals properly
       FIXME: Save and restore window size
       FIXME: Update window title with URI */
    gtk_widget_show_all (window);
    /* inspector_view = webkit_web_view_new ();
    gtk_widget_show (inspector_view);
    g_signal_emit (view, signals[ATTACH_INSPECTOR], 0, inspector_view); */
    return WEBKIT_WEB_VIEW (inspector_view);
}

static void
midori_view_construct_web_view (MidoriView* view)
{
    WebKitWebFrame* web_frame;
    gpointer inspector;

    g_return_if_fail (!view->web_view);

    view->web_view = webkit_web_view_new ();

    /* Load something to avoid a bug where WebKit might not set a main frame */
    webkit_web_view_open (WEBKIT_WEB_VIEW (view->web_view), "");
    web_frame = webkit_web_view_get_main_frame (WEBKIT_WEB_VIEW (view->web_view));

    g_object_connect (view->web_view,
                      "signal::load-started",
                      webkit_web_view_load_started_cb, view,
                      "signal::load-committed",
                      webkit_web_view_load_committed_cb, view,
                      "signal::load-progress-changed",
                      webkit_web_view_progress_changed_cb, view,
                      "signal::load-finished",
                      webkit_web_view_load_finished_cb, view,
                      #if WEBKIT_CHECK_VERSION (1, 1, 4)
                      "signal::notify::uri",
                      webkit_web_view_notify_uri_cb, view,
                      "signal::notify::title",
                      webkit_web_view_notify_title_cb, view,
                      #else
                      "signal::title-changed",
                      webkit_web_view_title_changed_cb, view,
                      #endif
                      "signal::status-bar-text-changed",
                      webkit_web_view_statusbar_text_changed_cb, view,
                      "signal::hovering-over-link",
                      webkit_web_view_hovering_over_link_cb, view,
                      "signal::button-press-event",
                      gtk_widget_button_press_event_cb, view,
                      "signal-after::key-press-event",
                      gtk_widget_key_press_event_cb, view,
                      "signal::scroll-event",
                      gtk_widget_scroll_event_cb, view,
                      "signal::populate-popup",
                      webkit_web_view_populate_popup_cb, view,
                      "signal::console-message",
                      webkit_web_view_console_message_cb, view,
                      "signal::window-object-cleared",
                      webkit_web_view_window_object_cleared_cb, view,
                      "signal::create-web-view",
                      webkit_web_view_create_web_view_cb, view,
                      "signal::mime-type-policy-decision-requested",
                      webkit_web_view_mime_type_decision_cb, view,
                      #if WEBKIT_CHECK_VERSION (1, 1, 3)
                      "signal::download-requested",
                      webkit_web_view_download_requested_cb, view,
                      #endif
                      #if WEBKIT_CHECK_VERSION (1, 1, 6)
                      "signal::load-error",
                      webkit_web_view_load_error_cb, view,
                      #endif
                      NULL);

    #if !WEBKIT_CHECK_VERSION (1, 1, 6)
    g_object_connect (web_frame,
                      "signal::load-done",
                      webkit_web_frame_load_done_cb, view,
                      NULL);
    #endif

    if (view->settings)
    {
        g_object_set (view->web_view, "settings", view->settings,
            "full-content-zoom", katze_object_get_boolean (view->settings,
                "zoom-text-and-images"), NULL);
    }

    gtk_widget_show (view->web_view);
    gtk_container_add (GTK_CONTAINER (view), view->web_view);

    inspector = katze_object_get_object (view->web_view, "web-inspector");
    g_object_connect (inspector, "signal::inspect-web-view",
                      webkit_web_inspector_inspect_web_view_cb, view, NULL);
}

/**
 * midori_view_set_uri:
 * @view: a #MidoriView
 *
 * Opens the specified URI in the view.
 **/
void
midori_view_set_uri (MidoriView*  view,
                     const gchar* uri)
{
    gchar* data;

    g_return_if_fail (MIDORI_IS_VIEW (view));

    /* Treat "about:blank" and "" equally, see midori_view_is_blank(). */
    if (!g_strcmp0 (uri, "about:blank")) uri = "";
    if (!uri) uri = "";

    if (1)
    {
        if (!view->web_view)
            midori_view_construct_web_view (view);

        if (view->speed_dial_in_new_tabs && !g_strcmp0 (uri, ""))
        {
            SoupServer* res_server;
            guint port;
            gchar* res_root;
            gchar* speed_dial_head;
            gchar* speed_dial_body;
            gchar* body_fname;
            gchar* stock_root;

            katze_assign (view->uri, g_strdup (""));

            g_file_get_contents (DATADIR "/midori/res/speeddial-head.html",
                                 &speed_dial_head, NULL, NULL);

            res_server = sokoke_get_res_server ();
            port = soup_server_get_port (res_server);
            res_root = g_strdup_printf ("http://localhost:%d/res", port);
            stock_root = g_strdup_printf ("http://localhost:%d/stock", port);
            body_fname = g_build_filename (sokoke_set_config_dir (NULL),
                                           "speeddial.json", NULL);

            if (!g_file_test (body_fname, G_FILE_TEST_EXISTS))
            {
                if (g_file_get_contents (DATADIR "/midori/res/speeddial.json",
                                         &speed_dial_body, NULL, NULL))
                    g_file_set_contents (body_fname, speed_dial_body, -1, NULL);
                else
                    speed_dial_body = g_strdup ("");
            }
            else
                g_file_get_contents (body_fname, &speed_dial_body, NULL, NULL);

            data = sokoke_replace_variables (speed_dial_head,
                "{res}", res_root,
                "{stock}", stock_root,
                "{json_data}", speed_dial_body,
                "{title}", _("Speed dial"),
                "{click_to_add}", _("Click to add a shortcut"),
                "{enter_shortcut_address}", _("Enter shortcut address"),
                "{enter_shortcut_name}", _("Enter shortcut title"),
                "{are_you_sure}", _("Are you sure you want to delete this shortcut?"), NULL);


            #if WEBKIT_CHECK_VERSION (1, 1, 6)
            webkit_web_frame_load_alternate_string (
                webkit_web_view_get_main_frame (WEBKIT_WEB_VIEW (view->web_view)),
                data, res_root, "about:blank");
            #else
            webkit_web_view_load_html_string (
                WEBKIT_WEB_VIEW (view->web_view), data, res_root);
            #endif

            g_free (res_root);
            g_free (stock_root);
            g_free (data);
            g_free (speed_dial_head);
            g_free (speed_dial_body);
            g_free (body_fname);
        }
        /* This is not prefectly elegant, but creating an
           error page inline is the simplest solution. */
        else if (g_str_has_prefix (uri, "error:"))
        {
            data = NULL;
            #if !WEBKIT_CHECK_VERSION (1, 1, 3)
            if (!strncmp (uri, "error:nodisplay ", 16))
            {
                gchar* title;

                katze_assign (view->uri, g_strdup (&uri[16]));
                title = g_strdup_printf (_("Document cannot be displayed"));
                data = g_strdup_printf (
                    "<html><head><title>%s</title></head>"
                    "<body><h1>%s</h1>"
                    "<img src=\"file://" DATADIR "/midori/logo-shade.png\" "
                    "style=\"position: absolute; right: 15px; bottom: 15px;\">"
                    "<p />The document %s of type '%s' cannot be displayed."
                    "</body></html>",
                    title, title, view->uri, view->mime_type);
                g_free (title);
            }
            #endif
            if (!strncmp (uri, "error:nodocs ", 13))
            {
                gchar* title;

                katze_assign (view->uri, g_strdup (&uri[13]));
                title = g_strdup_printf (_("No documentation installed"));
                data = g_strdup_printf (
                    "<html><head><title>%s</title></head>"
                    "<body><h1>%s</h1>"
                    "<img src=\"file://" DATADIR "/midori/logo-shade.png\" "
                    "style=\"position: absolute; right: 15px; bottom: 15px;\">"
                    "<p />There is no documentation installed at %s."
                    "You may want to ask your distribution or "
                    "package maintainer for it or if this a custom build "
                    "verify that the build is setup properly."
                    "</body></html>",
                    title, title, view->uri);
                g_free (title);
            }
            if (data)
            {
                webkit_web_view_load_html_string (
                    WEBKIT_WEB_VIEW (view->web_view), data, view->uri);
                g_free (data);
                g_object_notify (G_OBJECT (view), "uri");
                if (view->item)
                    katze_item_set_uri (view->item, uri);
                return;
            }
        }
        else if (g_str_has_prefix (uri, "javascript:"))
        {
            midori_view_execute_script (view, &uri[11], NULL);
        }
        else if (g_str_has_prefix (uri, "mailto:"))
        {
            sokoke_show_uri (NULL, uri, GDK_CURRENT_TIME, NULL);
        }
        else
        {
            katze_assign (view->uri, g_strdup (uri));
            g_object_notify (G_OBJECT (view), "uri");
            if (view->item)
                katze_item_set_uri (view->item, uri);
            webkit_web_view_open (WEBKIT_WEB_VIEW (view->web_view), uri);
        }
    }
}

/**
 * midori_view_is_blank:
 * @view: a #MidoriView
 *
 * Determines whether the view is currently empty.
 **/
gboolean
midori_view_is_blank (MidoriView*  view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), TRUE);

    return midori_view_get_display_uri (view)[0] == '\0';
}

/**
 * midori_view_get_icon:
 * @view: a #MidoriView
 *
 * Retrieves the icon of the view.
 *
 * Return value: a #GdkPixbuf
 **/
GdkPixbuf*
midori_view_get_icon (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);

    return view->icon;
}

/**
 * midori_view_get_display_uri:
 * @view: a #MidoriView
 *
 * Retrieves a string that is suitable for displaying.
 *
 * Note that "about:blank" is represented as "".
 *
 * You can assume that the string is not %NULL.
 *
 * Return value: an URI string
 **/
const gchar*
midori_view_get_display_uri (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), "");

    /* Something in the stack tends to turn "" into "about:blank".
       Yet for practical purposes we prefer "".  */
    if (view->uri && !strcmp (view->uri, "about:blank"))
        return "";

    if (view->uri && *view->uri)
        return view->uri;
    return "";
}

/**
 * midori_view_get_display_title:
 * @view: a #MidoriView
 *
 * Retrieves a string that is suitable for displaying
 * as a title. Most of the time this will be the title
 * or the current URI.
 *
 * An empty page is represented as "about:blank".
 *
 * You can assume that the string is not %NULL.
 *
 * Return value: a title string
 **/
const gchar*
midori_view_get_display_title (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), "about:blank");

    if (view->title && *view->title)
        return view->title;
    if (midori_view_is_blank (view))
        return _("Blank page");
    return midori_view_get_display_uri (view);
}

/**
 * midori_view_get_link_uri:
 * @view: a #MidoriView
 *
 * Retrieves the uri of the currently focused link,
 * particularly while the mouse hovers a link or a
 * context menu is being opened.
 *
 * Return value: an URI string, or %NULL if there is no link focussed
 **/
const gchar*
midori_view_get_link_uri (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);

    return view->link_uri;
}

/**
 * midori_view_has_selection:
 * @view: a #MidoriView
 *
 * Determines whether something in the view is selected.
 *
 * This function returns %FALSE if there is a selection
 * that effectively only consists of whitespace.
 *
 * Return value: %TRUE if effectively there is a selection
 **/
gboolean
midori_view_has_selection (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    katze_assign (view->selected_text, webkit_web_view_get_selected_text (
        WEBKIT_WEB_VIEW (view->web_view)));
    if (view->selected_text && *view->selected_text)
        return TRUE;
    else
        return FALSE;
}

/**
 * midori_view_get_selected_text:
 * @view: a #MidoriView
 *
 * Retrieves the currently selected text.
 *
 * Return value: the selected text, or %NULL
 **/
const gchar*
midori_view_get_selected_text (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);

    if (midori_view_has_selection (view))
        return view->selected_text;
    return NULL;
}

/**
 * midori_view_can_cut_clipboard:
 * @view: a #MidoriView
 *
 * Determines whether a selection can be cut.
 *
 * Return value: %TRUE if a selection can be cut
 **/
gboolean
midori_view_can_cut_clipboard (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    if (view->web_view)
        return webkit_web_view_can_cut_clipboard (
            WEBKIT_WEB_VIEW (view->web_view));
    else
        return FALSE;
}

/**
 * midori_view_can_copy_clipboard:
 * @view: a #MidoriView
 *
 * Determines whether a selection can be copied.
 *
 * Return value: %TRUE if a selection can be copied
 **/
gboolean
midori_view_can_copy_clipboard (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    if (view->web_view)
        return webkit_web_view_can_copy_clipboard (
            WEBKIT_WEB_VIEW (view->web_view));
    else
        return FALSE;
}

/**
 * midori_view_can_paste_clipboard:
 * @view: a #MidoriView
 *
 * Determines whether a selection can be pasted.
 *
 * Return value: %TRUE if a selection can be pasted
 **/
gboolean
midori_view_can_paste_clipboard (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    if (view->web_view)
        return webkit_web_view_can_paste_clipboard (
            WEBKIT_WEB_VIEW (view->web_view));
    else
        return FALSE;
}

/**
 * midori_view_get_proxy_menu_item:
 * @view: a #MidoriView
 *
 * Retrieves a proxy menu item that is typically added to a Window menu
 * and which on activation switches to the right window/ tab.
 *
 * The item is created on the first call and will be updated to reflect
 * changes to the icon and title automatically.
 *
 * The menu item is valid until it is removed from its container.
 *
 * Return value: the proxy #GtkMenuItem
 **/
GtkWidget*
midori_view_get_proxy_menu_item (MidoriView* view)
{
    const gchar* title;

    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);

    if (!view->menu_item)
    {
        title = midori_view_get_display_title (view);
        view->menu_item = sokoke_image_menu_item_new_ellipsized (title);
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (view->menu_item),
            gtk_image_new_from_pixbuf (view->icon));

        g_signal_connect (view->menu_item, "destroy",
                          G_CALLBACK (gtk_widget_destroyed),
                          &view->menu_item);
    }
    return view->menu_item;
}

static gboolean
midori_view_tab_label_button_release_event (GtkWidget*      tab_label,
                                            GdkEventButton* event,
                                            GtkWidget*      widget)
{
    if (event->button == 2)
    {
        /* Close the widget on middle click */
        gtk_widget_destroy (widget);
        return TRUE;
    }

    return FALSE;
}

static void
midori_view_tab_close_clicked (GtkWidget* tab_close,
                               GtkWidget* widget)
{
    gtk_widget_destroy (widget);
}

static void
midori_view_tab_icon_style_set_cb (GtkWidget* tab_icon,
                                   GtkStyle*  previous_style)
{
    GtkSettings* gtk_settings;
    gint width, height;

    gtk_settings = gtk_widget_get_settings (tab_icon);
    gtk_icon_size_lookup_for_settings (gtk_settings, GTK_ICON_SIZE_MENU,
                                       &width, &height);
    gtk_widget_set_size_request (tab_icon, width + 4, height + 4);
}

static void
midori_view_update_tab_title (GtkWidget* label,
                              gint       size,
                              gdouble    angle)
{
    gint width;

    sokoke_widget_get_text_size (label, "M", &width, NULL);
    if (angle == 0.0 || angle == 360.0)
    {
        gtk_widget_set_size_request (label, width * size, -1);
        if (gtk_label_get_ellipsize (GTK_LABEL (label)) != PANGO_ELLIPSIZE_START)
            gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    }
    else
    {
        gtk_widget_set_size_request (label, -1, width * size);
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_NONE);
    }
    gtk_label_set_angle (GTK_LABEL (label), angle);
}

static void
gtk_box_repack (GtkBox*    box,
                GtkWidget* child)
{
    GtkWidget* old_box;
    gboolean expand, fill;
    guint padding;
    GtkPackType pack_type;

    old_box = gtk_widget_get_parent (child);
    g_return_if_fail (GTK_IS_BOX (old_box));

    gtk_box_query_child_packing (GTK_BOX (old_box), child,
        &expand, &fill, &padding, &pack_type);

    g_object_ref (child);
    gtk_container_remove (GTK_CONTAINER (old_box), child);
    if (pack_type == GTK_PACK_START)
        gtk_box_pack_start (box, child, expand, fill, padding);
    else
        gtk_box_pack_end (box, child, expand, fill, padding);
    g_object_unref (child);
}

static void
midori_view_tab_label_parent_set (GtkWidget*  tab_label,
                                  GtkObject*  old_parent,
                                  MidoriView* view)
{
    GtkWidget* parent;

    /* FIXME: Disconnect orientation notification
    if (old_parent)
        ; */

    if (!(parent = gtk_widget_get_parent (tab_label)))
        return;

    if (GTK_IS_NOTEBOOK (parent))
    {
        GtkPositionType pos;
        gdouble old_angle, angle;
        GtkWidget* box;

        pos = gtk_notebook_get_tab_pos (GTK_NOTEBOOK (parent));
        old_angle = gtk_label_get_angle (GTK_LABEL (view->tab_title));
        switch (pos)
        {
        case GTK_POS_LEFT:
            angle = 90.0;
            break;
        case GTK_POS_RIGHT:
            angle = 270.0;
            break;
        default:
            angle = 0.0;
        }

        if (old_angle != angle)
        {
            if (angle == 0.0)
                box = gtk_hbox_new (FALSE, 1);
            else
                box = gtk_vbox_new (FALSE, 1);
            gtk_box_repack (GTK_BOX (box), view->tab_icon);
            gtk_box_repack (GTK_BOX (box), view->tab_title);
            gtk_box_repack (GTK_BOX (box), view->tab_close);

            gtk_container_remove (GTK_CONTAINER (tab_label),
                gtk_bin_get_child (GTK_BIN (tab_label)));
            gtk_container_add (GTK_CONTAINER (tab_label), GTK_WIDGET (box));
            gtk_widget_show (box);
        }

        midori_view_update_tab_title (view->tab_title, 10, angle);

        /* FIXME: Connect orientation notification */
    }
}

/**
 * midori_view_get_proxy_tab_label:
 * @view: a #MidoriView
 *
 * Retrieves a proxy tab label that is typically used when
 * adding the view to a notebook.
 *
 * Note that the label actually adjusts its orientation
 * to the according tab position when used in a notebook.
 *
 * The label is created on the first call and will be updated to reflect
 * changes of the loading progress and title.
 *
 * The label is valid until it is removed from its container.
 *
 * Return value: the proxy #GtkEventBox
 **/
GtkWidget*
midori_view_get_proxy_tab_label (MidoriView* view)
{
    GtkWidget* event_box;
    GtkWidget* hbox;
    GtkRcStyle* rcstyle;
    GtkWidget* image;

    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);

    if (!view->tab_label)
    {
        view->tab_icon = katze_throbber_new ();
        katze_throbber_set_static_pixbuf (KATZE_THROBBER (view->tab_icon),
            midori_view_get_icon (view));
        gtk_misc_set_alignment (GTK_MISC (view->tab_icon), 0.0, 0.5);

        view->tab_title = gtk_label_new (midori_view_get_display_title (view));
        gtk_misc_set_alignment (GTK_MISC (view->tab_title), 0.0, 0.5);

        event_box = gtk_event_box_new ();
        gtk_event_box_set_visible_window (GTK_EVENT_BOX (event_box), FALSE);
        hbox = gtk_hbox_new (FALSE, 1);
        gtk_container_add (GTK_CONTAINER (event_box), GTK_WIDGET (hbox));
        midori_view_update_tab_title (view->tab_title, 10, 0.0);

        view->tab_close = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (view->tab_close), GTK_RELIEF_NONE);
        gtk_button_set_focus_on_click (GTK_BUTTON (view->tab_close), FALSE);
        rcstyle = gtk_rc_style_new ();
        rcstyle->xthickness = rcstyle->ythickness = 0;
        gtk_widget_modify_style (view->tab_close, rcstyle);
        g_object_unref (rcstyle);
        image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
        gtk_button_set_image (GTK_BUTTON (view->tab_close), image);
        gtk_misc_set_alignment (GTK_MISC (image), 0.0, 0.5);

        #if HAVE_OSX
        gtk_box_pack_end (GTK_BOX (hbox), view->tab_icon, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (hbox), view->tab_title, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), view->tab_close, FALSE, FALSE, 0);
        #else
        gtk_box_pack_start (GTK_BOX (hbox), view->tab_icon, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), view->tab_title, FALSE, TRUE, 0);
        gtk_box_pack_end (GTK_BOX (hbox), view->tab_close, FALSE, FALSE, 0);
        #endif
        gtk_widget_show_all (GTK_WIDGET (event_box));

        if (!view->close_buttons_on_tabs)
            gtk_widget_hide (view->tab_close);

        g_signal_connect (event_box, "button-release-event",
            G_CALLBACK (midori_view_tab_label_button_release_event), view);
        g_signal_connect (view->tab_close, "style-set",
            G_CALLBACK (midori_view_tab_icon_style_set_cb), NULL);
        g_signal_connect (view->tab_close, "clicked",
            G_CALLBACK (midori_view_tab_close_clicked), view);

        view->tab_label = event_box;
        g_signal_connect (view->tab_icon, "destroy",
                          G_CALLBACK (gtk_widget_destroyed),
                          &view->tab_icon);
        g_signal_connect (view->tab_label, "destroy",
                          G_CALLBACK (gtk_widget_destroyed),
                          &view->tab_label);

        g_signal_connect (view->tab_label, "parent-set",
                          G_CALLBACK (midori_view_tab_label_parent_set),
                          view);
    }
    return view->tab_label;
}

/**
 * midori_view_get_proxy_item:
 * @view: a #MidoriView
 *
 * Retrieves a proxy item that can be used for bookmark storage as
 * well as session management.
 *
 * The item is created on the first call and will be updated to reflect
 * changes to the title and uri automatically.
 *
 * Return value: the proxy #KatzeItem
 **/
KatzeItem*
midori_view_get_proxy_item (MidoriView* view)
{
    const gchar* uri;
    const gchar* title;

    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);

    if (!view->item)
    {
        view->item = katze_item_new ();
        uri = midori_view_get_display_uri (view);
        katze_item_set_uri (view->item, uri);
        title = midori_view_get_display_title (view);
        katze_item_set_name (view->item, title);
    }
    return view->item;
}

/**
 * midori_view_get_zoom_level:
 * @view: a #MidoriView
 *
 * Determines the current zoom level of the view.
 *
 * Return value: the current zoom level
 **/
gfloat
midori_view_get_zoom_level (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), 1.0f);

    if (view->web_view != NULL)
        return webkit_web_view_get_zoom_level (WEBKIT_WEB_VIEW (view->web_view));
    return 1.0f;
}

/**
 * midori_view_set_zoom_level:
 * @view: a #MidoriView
 * @zoom_level: the new zoom level
 *
 * Sets the current zoom level of the view.
 **/
void
midori_view_set_zoom_level (MidoriView* view,
                            gfloat      zoom_level)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_set_zoom_level (
        WEBKIT_WEB_VIEW (view->web_view), zoom_level);
    g_object_notify (G_OBJECT (view), "zoom-level");
}

gboolean
midori_view_can_zoom_in (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    return view->web_view != NULL;
}

gboolean
midori_view_can_zoom_out (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    return view->web_view != NULL;
}

gboolean
midori_view_can_view_source (MidoriView* view)
{
    const gchar* uri = view->uri;

    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    /* FIXME: Consider other types that are also text */
    if (!g_str_has_prefix (view->mime_type, "text/")
        && !g_strrstr (view->mime_type, "xml"))
        return FALSE;

    if (!uri)
        return FALSE;
    if (g_str_has_prefix (uri, "http://") || g_str_has_prefix (uri, "https://"))
        return TRUE;
    if (g_str_has_prefix (uri, "file://"))
        return TRUE;
    return FALSE;
}

#define can_do(what) \
gboolean \
midori_view_can_##what (MidoriView* view) \
{ \
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE); \
\
    return view->web_view != NULL; \
}

can_do (reload)
can_do (print)
can_do (find)

/**
 * midori_view_reload:
 * @view: a #MidoriView
 * @from_cache: whether to allow caching
 *
 * Reloads the view.
 *
 * Note: The @from_cache value is currently ignored.
 **/
void
midori_view_reload (MidoriView* view,
                    gboolean    from_cache)
{
    gchar* title;

    g_return_if_fail (MIDORI_IS_VIEW (view));

#if WEBKIT_CHECK_VERSION (1, 1, 6)
    /* WebKit 1.1.6 doesn't handle "alternate content" flawlessly,
       so reloading via Javascript works but not via API calls. */
    title = g_strdup (_("Error"));
#else
    /* Error pages are special, we want to try loading the destination
       again, not the error page which isn't even a proper page */
    title = g_strdup_printf (_("Not found - %s"), view->uri);
#endif
    if (view->title && strstr (title, view->title))
        webkit_web_view_open (WEBKIT_WEB_VIEW (view->web_view), view->uri);
    else if (from_cache)
        webkit_web_view_reload (WEBKIT_WEB_VIEW (view->web_view));
    else
        webkit_web_view_reload_bypass_cache (WEBKIT_WEB_VIEW (view->web_view));

    g_free (title);
}

/**
 * midori_view_stop_loading
 * @view: a #MidoriView
 *
 * Stops loading the view if it is currently loading.
 **/
void
midori_view_stop_loading (MidoriView* view)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_stop_loading (WEBKIT_WEB_VIEW (view->web_view));
}

/**
 * midori_view_can_go_back
 * @view: a #MidoriView
 *
 * Determines whether the view can go back.
 **/
gboolean
midori_view_can_go_back (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    if (view->web_view)
        return webkit_web_view_can_go_back (WEBKIT_WEB_VIEW (view->web_view));
    else
        return FALSE;
}

/**
 * midori_view_go_back
 * @view: a #MidoriView
 *
 * Goes back one page in the view.
 **/
void
midori_view_go_back (MidoriView* view)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_go_back (WEBKIT_WEB_VIEW (view->web_view));
}

/**
 * midori_view_can_go_forward
 * @view: a #MidoriView
 *
 * Determines whether the view can go forward.
 **/
gboolean
midori_view_can_go_forward (MidoriView* view)
{
    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);

    if (view->web_view)
        return webkit_web_view_can_go_forward (WEBKIT_WEB_VIEW (view->web_view));
    else
        return FALSE;
}

/**
 * midori_view_go_forward
 * @view: a #MidoriView
 *
 * Goes forward one page in the view.
 **/
void
midori_view_go_forward (MidoriView* view)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_go_forward (WEBKIT_WEB_VIEW (view->web_view));
}

/**
 * midori_view_print
 * @view: a #MidoriView
 *
 * Prints the contents of the view.
 **/
void
midori_view_print (MidoriView* view)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_frame_print (webkit_web_view_get_main_frame (
        WEBKIT_WEB_VIEW (view->web_view)));
}

/**
 * midori_view_unmark_text_matches
 * @view: a #MidoriView
 *
 * Unmarks the text matches in the view.
 **/
void
midori_view_unmark_text_matches (MidoriView* view)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_unmark_text_matches (WEBKIT_WEB_VIEW (view->web_view));
}

/**
 * midori_view_search_text
 * @view: a #MidoriView
 * @text: a string
 * @case_sensitive: case sensitivity
 * @forward: whether to search forward
 *
 * Searches a text within the view.
 **/
void
midori_view_search_text (MidoriView*  view,
                         const gchar* text,
                         gboolean     case_sensitive,
                         gboolean     forward)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    g_signal_emit (view, signals[SEARCH_TEXT], 0,
        webkit_web_view_search_text (WEBKIT_WEB_VIEW (view->web_view),
            text, case_sensitive, forward, TRUE), NULL);
}

/**
 * midori_view_mark_text_matches
 * @view: a #MidoriView
 * @text: a string
 * @case_sensitive: case sensitivity
 *
 * Marks all text matches within the view.
 **/
void
midori_view_mark_text_matches (MidoriView*  view,
                               const gchar* text,
                               gboolean     case_sensitive)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_mark_text_matches (WEBKIT_WEB_VIEW (view->web_view),
        text, case_sensitive, 0);
}

/**
 * midori_view_set_highlight_text_matches
 * @view: a #MidoriView
 * @highlight: whether to highlight matches
 *
 * Whether to highlight all matches within the view.
 **/
void
midori_view_set_highlight_text_matches (MidoriView* view,
                                        gboolean    highlight)
{
    g_return_if_fail (MIDORI_IS_VIEW (view));

    webkit_web_view_set_highlight_text_matches (
        WEBKIT_WEB_VIEW (view->web_view), highlight);
}

/**
 * midori_view_execute_script
 * @view: a #MidoriView
 * @script: script code
 * @exception: location to store an exception message
 *
 * Execute a script on the view.
 *
 * Returns: %TRUE if the script was executed successfully
 **/
gboolean
midori_view_execute_script (MidoriView*  view,
                            const gchar* script,
                            gchar**      exception)
{
    WebKitWebFrame* web_frame;
    JSContextRef js_context;
    gchar* script_decoded;

    g_return_val_if_fail (MIDORI_IS_VIEW (view), FALSE);
    g_return_val_if_fail (script != NULL, FALSE);

    /* FIXME Actually store exception. */
    web_frame = webkit_web_view_get_main_frame (WEBKIT_WEB_VIEW (view->web_view));
    js_context = webkit_web_frame_get_global_context (web_frame);
    if ((script_decoded = soup_uri_decode (script)))
    {
        webkit_web_view_execute_script (WEBKIT_WEB_VIEW (view->web_view),
                                        script_decoded);
        g_free (script_decoded);
    }
    else
        webkit_web_view_execute_script (WEBKIT_WEB_VIEW (view->web_view), script);
    return TRUE;
}

/* For now this is private API */
GdkPixbuf*
midori_view_get_snapshot (MidoriView* view,
                          guint       width,
                          guint       height)
{
    GtkWidget* web_view;
    GdkRectangle rect;
    GdkPixmap* pixmap;
    GdkEvent event;
    gboolean result;
    GdkColormap* colormap;
    GdkPixbuf* pixbuf;

    g_return_val_if_fail (MIDORI_IS_VIEW (view), NULL);
    web_view = gtk_bin_get_child (GTK_BIN (view));
    g_return_val_if_fail (web_view->window, NULL);

    rect.x = web_view->allocation.x;
    rect.y = web_view->allocation.y;
    rect.width = web_view->allocation.width;
    rect.height = web_view->allocation.height;

    pixmap = gdk_pixmap_new (web_view->window,
        web_view->allocation.width, web_view->allocation.height,
        gdk_drawable_get_depth (web_view->window));
    event.expose.type = GDK_EXPOSE;
    event.expose.window = pixmap;
    event.expose.send_event = FALSE;
    event.expose.count = 0;
    event.expose.area.x = 0;
    event.expose.area.y = 0;
    gdk_drawable_get_size (GDK_DRAWABLE (web_view->window),
        &event.expose.area.width, &event.expose.area.height);
    event.expose.region = gdk_region_rectangle (&event.expose.area);

    g_signal_emit_by_name (web_view, "expose-event", &event, &result);

    colormap = gdk_drawable_get_colormap (pixmap);
    pixbuf = gdk_pixbuf_get_from_drawable (NULL, pixmap, colormap, 0, 0,
                                           0, 0, rect.width, rect.height);
    g_object_unref (pixmap);

    if (width || height)
    {
        GdkPixbuf* scaled;
        if (!width)
            width = rect.width;
        if (!height)
            height = rect.height;
        scaled = gdk_pixbuf_scale_simple (pixbuf, width, height,
                                          GDK_INTERP_TILES);
        g_object_unref (pixbuf);
        return scaled;
    }

    return pixbuf;
}

static void
thumb_view_load_status_cb (MidoriView* thumb_view,
                           GParamSpec* pspec,
                           MidoriView* view)
{
    GdkPixbuf* img;
    gchar* file_content;
    gchar* encoded;
    gchar* dom_id;
    gchar* js;
    gsize sz;

    if (katze_object_get_enum (thumb_view, "load-status") != MIDORI_LOAD_FINISHED)
        return;

    img = midori_view_get_snapshot (MIDORI_VIEW (thumb_view), 160, 107);
    gdk_pixbuf_save_to_buffer (img, &file_content, &sz, "png", NULL, "compression", "7", NULL);
    encoded = g_base64_encode ((guchar *)file_content, sz );

    /* Call Javascript function to replace shortcut's content */
    dom_id = g_object_get_data (G_OBJECT (thumb_view), "dom-id");
    js = g_strdup_printf ("setThumbnail('%s','%s','%s');",
                          dom_id, encoded, thumb_view->uri);
    webkit_web_view_execute_script (WEBKIT_WEB_VIEW (view->web_view), js);
    free (js);
    g_object_unref (img);

    g_free (dom_id);
    g_free (encoded);
    g_free (file_content);

    gtk_widget_destroy (GTK_WIDGET (thumb_view));
}

/**
 * midori_view_speed_dial_inject_thumb
 * @view: a #MidoriView
 * @filename: filename of the thumbnail
 * @dom_id: Id of the shortcut on speed_dial page in wich to inject content
 * @url: url of the shortcut
 */
static void
midori_view_speed_dial_inject_thumb (MidoriView* view,
                                     gchar*      filename,
                                     gchar*      dom_id,
                                     gchar*      url)
{
    GtkWidget* thumb_view;
    MidoriWebSettings* settings;
    GtkWidget* browser;
    GtkWidget* notebook;
    GtkWidget* label;

    thumb_view = midori_view_new (view->net);
    settings = g_object_new (MIDORI_TYPE_WEB_SETTINGS, "enable-scripts", FALSE,
        "enable-plugins", FALSE, "auto-load-images", TRUE, NULL);
    midori_view_set_settings (MIDORI_VIEW (thumb_view), settings);
    browser = gtk_widget_get_toplevel (GTK_WIDGET (view));
    if (!GTK_IS_WINDOW (browser))
        return;
    /* What we are doing here is a bit of a hack. In order to render a
       thumbnail we need a new view and load the url in it. But it has
       to be visible and packed in a container. So we secretly pack it
       into the notebook of the parent browser. */
    notebook = katze_object_get_object (browser, "notebook");
    if (!notebook)
        return;
    gtk_container_add (GTK_CONTAINER (notebook), thumb_view);
    /* We use an empty label. It's not invisible but at least hard to spot. */
    label = gtk_event_box_new ();
    gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook), thumb_view, label);
    g_object_unref (notebook);
    gtk_widget_show (thumb_view);
    g_object_set_data (G_OBJECT (thumb_view), "dom-id", dom_id);
    g_signal_connect (thumb_view, "notify::load-status",
        G_CALLBACK (thumb_view_load_status_cb), view);
    midori_view_set_uri (MIDORI_VIEW (thumb_view), url);
}

/**
 * midori_view_speed_dial_save
 * @web_view: a #WebkitView
 * @message: Console log data
 *
 * Load a thumbnail, and set the DOM
 *
 * message[0] == console message call
 * message[1] == shortcut id in the DOM
 * message[2] == shortcut uri
 *
 **/
static void
midori_view_speed_dial_get_thumb (GtkWidget*   web_view,
                                  const gchar* message,
                                  MidoriView*  view)
{
    gchar** t_data = g_strsplit (message," ", 4);

    if (t_data[1] == NULL || t_data[2] == NULL )
        return;

    midori_view_speed_dial_inject_thumb (view, NULL,
        g_strdup (t_data[1]), g_strdup (t_data[2]));
    g_strfreev (t_data);
}

/**
 * midori_view_speed_dial_save
 * @web_view: a #WebkitView
 *
 * Save speed_dial DOM structure to body template
 *
 **/
static void
midori_view_speed_dial_save (GtkWidget*   web_view,
                             const gchar* message)
{
    gchar* json = g_strdup (message + 15);
    gchar* fname = g_build_filename (sokoke_set_config_dir (NULL),
                                     "speeddial.json", NULL);

    GRegex* reg_double = g_regex_new ("\\\\\"", 0, 0, NULL);
    gchar* safe = g_regex_replace_literal (reg_double, json, -1, 0, "\\\\\"", 0, NULL);
    g_file_set_contents (fname, safe, -1, NULL);

    g_free (fname);
    g_free (json);
    g_free (safe);
    g_regex_unref (reg_double);
}
