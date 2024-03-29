/*
 Copyright (C) 2008-2009 Christian Dywan <christian@twotoasts.de>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#ifndef __COMPAT_H__
#define __COMPAT_H__

#if HAVE_CONFIG_H
    #include <config.h>
#endif

#include <webkit/webkit.h>

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION (2, 14, 0)
    #define G_PARAM_STATIC_STRINGS \
    (G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB)
#endif

#if !GTK_CHECK_VERSION (2, 14, 0)

gboolean
gtk_show_uri                           (GdkScreen*         screen,
                                        const gchar*       uri,
                                        guint32            timestamp,
                                        GError**           error);

#endif

#if !GTK_CHECK_VERSION(2, 12, 0)

void
gtk_widget_set_has_tooltip             (GtkWidget*         widget,
                                        gboolean           has_tooltip);

void
gtk_widget_set_tooltip_text            (GtkWidget*         widget,
                                        const gchar*       text);

void
gtk_tool_item_set_tooltip_text         (GtkToolItem*       toolitem,
                                        const gchar*       text);

#endif

G_END_DECLS

#endif /* __COMPAT_H__ */
