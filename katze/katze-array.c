/*
 Copyright (C) 2008-2009 Christian Dywan <christian@twotoasts.de>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#include "katze-array.h"

#include "katze-utils.h"
#include "marshal.h"

#include <glib/gi18n.h>
#include <string.h>

/**
 * SECTION:katze-array
 * @short_description: A type aware item container
 * @see_also: #KatzeList
 *
 * #KatzeArray is a type aware container for items.
 */

struct _KatzeArray
{
    KatzeItem parent_instance;

    GType type;
    GList* items;
};

struct _KatzeArrayClass
{
    KatzeItemClass parent_class;

    /* Signals */
    void
    (*add_item)               (KatzeArray* array,
                               gpointer    item);
    void
    (*remove_item)            (KatzeArray* array,
                               gpointer    item);
    void
    (*move_item)              (KatzeArray* array,
                               gpointer    item,
                               gint        index);
    void
    (*clear)                  (KatzeArray* array);
};

G_DEFINE_TYPE (KatzeArray, katze_array, KATZE_TYPE_ITEM);

enum {
    ADD_ITEM,
    REMOVE_ITEM,
    MOVE_ITEM,
    CLEAR,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
katze_array_finalize (GObject* object);

static void
_katze_array_add_item (KatzeArray* array,
                       gpointer    item)
{
    if (katze_array_is_a (array, G_TYPE_OBJECT))
    {
        GType type = G_OBJECT_TYPE (item);

        g_return_if_fail (katze_array_is_a (array, type));
        g_object_ref (item);
        if (g_type_is_a (type, KATZE_TYPE_ITEM))
            katze_item_set_parent (item, array);
    }

    array->items = g_list_append (array->items, item);
}

static void
_katze_array_remove_item (KatzeArray* array,
                          gpointer   item)
{
    array->items = g_list_remove (array->items, item);

    if (katze_array_is_a (array, G_TYPE_OBJECT))
    {
        if (KATZE_IS_ITEM (item))
            katze_item_set_parent (item, NULL);
        g_object_unref (item);
    }
}

static void
_katze_array_move_item (KatzeArray* array,
                        gpointer    item,
                        gint        position)
{
    array->items = g_list_remove (array->items, item);
    array->items = g_list_insert (array->items, item, position);
}

static void
_katze_array_clear (KatzeArray* array)
{
    GObject* item;

    while ((item = g_list_nth_data (array->items, 0)))
        katze_array_remove_item (array, item);
    g_list_free (array->items);
    array->items = NULL;
}

static void
katze_array_class_init (KatzeArrayClass* class)
{
    GObjectClass* gobject_class;

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = katze_array_finalize;

    signals[ADD_ITEM] = g_signal_new (
        "add-item",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        G_STRUCT_OFFSET (KatzeArrayClass, add_item),
        0,
        NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

    signals[REMOVE_ITEM] = g_signal_new (
        "remove-item",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        G_STRUCT_OFFSET (KatzeArrayClass, remove_item),
        0,
        NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

    /**
     * KatzeArray::move-item:
     * @array: the object on which the signal is emitted
     * @item: the item being moved
     * @position: the new position of the item
     *
     * An item is moved to a new position.
     *
     * Since: 0.1.6
     **/
    signals[MOVE_ITEM] = g_signal_new (
        "move-item",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        G_STRUCT_OFFSET (KatzeArrayClass, move_item),
        0,
        NULL,
        katze_cclosure_marshal_VOID__POINTER_INT,
        G_TYPE_NONE, 2,
        G_TYPE_POINTER,
        G_TYPE_INT);

    signals[CLEAR] = g_signal_new (
        "clear",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        G_STRUCT_OFFSET (KatzeArrayClass, clear),
        0,
        NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = katze_array_finalize;

    class->add_item = _katze_array_add_item;
    class->remove_item = _katze_array_remove_item;
    class->move_item = _katze_array_move_item;
    class->clear = _katze_array_clear;
}

static void
katze_array_init (KatzeArray* array)
{
    array->type = G_TYPE_NONE;
    array->items = NULL;
}

static void
katze_array_finalize (GObject* object)
{
    KatzeArray* array;
    guint i;

    array = KATZE_ARRAY (object);
    if (katze_array_is_a (array, G_TYPE_OBJECT))
    {
        gpointer item;
        i = 0;
        while ((item = g_list_nth_data (array->items, i++)))
            g_object_unref (item);
    }

    g_list_free (array->items);

    G_OBJECT_CLASS (katze_array_parent_class)->finalize (object);
}

/**
 * katze_array_new:
 * @type: the expected item type
 *
 * Creates a new #KatzeArray for @type items.
 *
 * You may only add items of the given type or inherited
 * from it to this array *if* @type is an #GObject type.
 * The array will keep a reference on each object until
 * it is removed from the array.
 *
 * Return value: a new #KatzeArray
 **/
KatzeArray*
katze_array_new (GType type)
{
    KatzeArray* array = g_object_new (KATZE_TYPE_ARRAY, NULL);
    array->type = type;

    return array;
}

/**
 *
 * katze_array_is_a:
 * @array: a #KatzeArray
 * @is_a_type: type to compare with
 *
 * Checks whether the array is compatible
 * with items of the specified type.
 *
 * Retur value: %TRUE if @array is compatible with @is_a_type
 **/
gboolean
katze_array_is_a (KatzeArray* array,
                  GType       is_a_type)
{
    g_return_val_if_fail (KATZE_IS_ARRAY (array), FALSE);

    return g_type_is_a (array->type, is_a_type);
}

/**
 * katze_array_add_item:
 * @array: a #KatzeArray
 * @item: an item
 *
 * Adds an item to the array.
 *
 * If @item is a #KatzeItem its parent is set accordingly.
 **/
void
katze_array_add_item (KatzeArray* array,
                      gpointer    item)
{
    g_return_if_fail (KATZE_IS_ARRAY (array));

    g_signal_emit (array, signals[ADD_ITEM], 0, item);
}

/**
 * katze_array_remove_item:
 * @array: a #KatzeArray
 * @item: an item
 *
 * Removes an item from the array.
 *
 * If @item is a #KatzeItem its parent is unset accordingly.
 **/
void
katze_array_remove_item (KatzeArray* array,
                         gpointer    item)
{
    g_return_if_fail (KATZE_IS_ARRAY (array));

    g_signal_emit (array, signals[REMOVE_ITEM], 0, item);
}

/**
 * katze_array_get_nth_item:
 * @array: a #KatzeArray
 * @n: an index in the array
 *
 * Retrieves the item in @array at the position @n.
 *
 * Return value: an item, or %NULL
 **/
gpointer
katze_array_get_nth_item (KatzeArray* array,
                          guint       n)
{
    g_return_val_if_fail (KATZE_IS_ARRAY (array), NULL);

    return g_list_nth_data (array->items, n);
}

/**
 * katze_array_is_empty:
 * @array: a #KatzeArray
 *
 * Determines whether @array is empty.
 *
 * Return value: an item, or %NULL
 **/
gboolean
katze_array_is_empty (KatzeArray* array)
{
    g_return_val_if_fail (KATZE_IS_ARRAY (array), TRUE);

    return !g_list_nth_data (array->items, 0);
}

/**
 * katze_array_get_item_index:
 * @array: a #KatzeArray
 * @item: an item in the array
 *
 * Retrieves the index of the item in @array.
 *
 * Return value: an item, or -1
 **/
gint
katze_array_get_item_index (KatzeArray* array,
                            gpointer    item)
{
    g_return_val_if_fail (KATZE_IS_ARRAY (array), -1);

    return g_list_index (array->items, item);
}

/**
 * katze_array_find_token:
 * @array: a #KatzeArray
 * @token: a token string
 *
 * Looks up an item in the array which has the specified token.
 *
 * This function will silently fail if the type of the list
 * is not based on #GObject and only #KatzeItem children
 * are checked for their token, any other objects are skipped.
 *
 * Note that @token is by definition unique to one item.
 *
 * Return value: an item, or %NULL
 **/
gpointer
katze_array_find_token (KatzeArray*  array,
                        const gchar* token)
{
    guint i;
    gpointer item;

    if (!katze_array_is_a (array, G_TYPE_OBJECT))
        return NULL;

    i = 0;
    while ((item = g_list_nth_data (array->items, i++)))
    {
        const gchar* found_token = katze_item_get_token ((KatzeItem*)item);
        if (!g_strcmp0 (found_token, token))
            return item;
    }
    return NULL;
}

/**
 * katze_array_get_length:
 * @array: a #KatzeArray
 *
 * Retrieves the number of items in @array.
 *
 * Return value: the length of the list
 **/
guint
katze_array_get_length (KatzeArray* array)
{
    g_return_val_if_fail (KATZE_IS_ARRAY (array), 0);

    return g_list_length (array->items);
}

/**
 * katze_array_move_item:
 * @array: a #KatzeArray
 * @item: the item being moved
 * @position: the new position of the item
 *
 * Moves @item to the position @position.
 *
 * Since: 0.1.6
 **/
void
katze_array_move_item (KatzeArray* array,
                       gpointer    item,
                       gint        position)
{
    g_return_if_fail (KATZE_IS_ARRAY (array));

    g_signal_emit (array, signals[MOVE_ITEM], 0, item, position);
}

/**
 * katze_array_clear:
 * @array: a #KatzeArray
 *
 * Deletes all items currently contained in @array.
 **/
void
katze_array_clear (KatzeArray* array)
{
    g_return_if_fail (KATZE_IS_ARRAY (array));

    g_signal_emit (array, signals[CLEAR], 0);
}
