/*
 * ewindow.c - Extended window object
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "luaa.h"
#include "xwindow.h"
#include "ewmh.h"
#include "screen.h"
#include "objects/window.h"
#include "objects/tag.h"
#include "common/luaobject.h"
#include "common/xutil.h"

LUA_CLASS_FUNCS(ewindow, (lua_class_t *) &ewindow_class)

bool
ewindow_isvisible(ewindow_t *ewindow)
{
    if(ewindow->minimized)
        return false;

    if(ewindow->sticky)
        return true;

    foreach(tag, ewindow->screen->tags)
        if(tag_get_selected(*tag) && ewindow_is_tagged(ewindow, *tag))
            return true;

    return false;
}

/** Return ewindow struts (reserved space at the edge of the screen).
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_struts(lua_State *L)
{
    ewindow_t *ewindow = luaA_checkudata(L, 1, (lua_class_t *) &ewindow_class);

    if(lua_gettop(L) == 2)
    {
        luaA_tostrut(L, 2, &ewindow->strut);
        luaA_object_emit_signal(L, 1, "property::struts", 0);
        if(ewindow->screen)
            screen_emit_signal(L, ewindow->screen, "property::workarea", 0);
    }

    return luaA_pushstrut(L, ewindow->strut);
}

/** Set a ewindow minimized, or not.
 * \param L The Lua VM state.
 * \param cidx The ewindow index.
 * \param s Set or not the ewindow minimized.
 */
void
ewindow_set_minimized(lua_State *L, int cidx, bool s)
{
    ewindow_t *ewindow = luaA_checkudata(L, cidx, (lua_class_t *) &ewindow_class);

    if(ewindow->minimized != s)
    {
        ewindow->minimized = s;
        if(s)
            xwindow_set_state(ewindow->window, XCB_WM_STATE_ICONIC);
        else
            xwindow_set_state(ewindow->window, XCB_WM_STATE_NORMAL);
        if(strut_has_value(&ewindow->strut))
            screen_emit_signal(globalconf.L, ewindow->screen, "property::workarea", 0);
        luaA_object_emit_signal(L, cidx, "property::minimized", 0);
    }
}

/** Set a ewindow fullscreen, or not.
 * \param L The Lua VM state.
 * \param cidx The ewindow index.
 * \param s Set or not the ewindow fullscreen.
 */
void
ewindow_set_fullscreen(lua_State *L, int cidx, bool s)
{
    ewindow_t *ewindow = luaA_checkudata(L, cidx, (lua_class_t *) &ewindow_class);

    if(ewindow->fullscreen != s)
    {
        /* become fullscreen! */
        if(s)
        {
            /* remove any max state */
            ewindow_set_maximized_horizontal(L, cidx, false);
            ewindow_set_maximized_vertical(L, cidx, false);
            /* You can only be part of one of the special layers. */
            ewindow_set_below(L, cidx, false);
            ewindow_set_above(L, cidx, false);
            ewindow_set_ontop(L, cidx, false);
        }
        int abs_cidx = luaA_absindex(L, cidx); \
        lua_pushboolean(L, s);
        luaA_object_emit_signal(L, abs_cidx, "request::fullscreen", 1);
        ewindow->fullscreen = s;
        luaA_object_emit_signal(L, abs_cidx, "property::fullscreen", 0);
    }
}

/** Set a ewindow horizontally|vertically maximized.
 * \param L The Lua VM state.
 * \param cidx The ewindow index.
 * \param s The maximized status.
 */
#define DO_FUNCTION_CLIENT_MAXIMIZED(type) \
    void \
    ewindow_set_maximized_##type(lua_State *L, int cidx, bool s) \
    { \
        ewindow_t *ewindow = luaA_checkudata(L, cidx, (lua_class_t *) &ewindow_class); \
        if(ewindow->maximized_##type != s) \
        { \
            int abs_cidx = luaA_absindex(L, cidx); \
            if(s) \
                ewindow_set_fullscreen(L, abs_cidx, false); \
            lua_pushboolean(L, s); \
            luaA_object_emit_signal(L, abs_cidx, "request::maximized_" #type, 1); \
            ewindow->maximized_##type = s; \
            luaA_object_emit_signal(L, abs_cidx, "property::maximized_" #type, 0); \
        } \
    }
DO_FUNCTION_CLIENT_MAXIMIZED(vertical)
DO_FUNCTION_CLIENT_MAXIMIZED(horizontal)
#undef DO_FUNCTION_CLIENT_MAXIMIZED

/** Set a ewindow above, or not.
 * \param L The Lua VM state.
 * \param cidx The ewindow index.
 * \param s Set or not the ewindow above.
 */
void
ewindow_set_above(lua_State *L, int cidx, bool s)
{
    ewindow_t *ewindow = luaA_checkudata(L, cidx, (lua_class_t *) &ewindow_class);

    if(ewindow->above != s)
    {
        /* You can only be part of one of the special layers. */
        if(s)
        {
            ewindow_set_below(L, cidx, false);
            ewindow_set_ontop(L, cidx, false);
            ewindow_set_fullscreen(L, cidx, false);
        }
        ewindow->above = s;
        luaA_object_emit_signal(L, cidx, "property::above", 0);
    }
}

/** Set a ewindow below, or not.
 * \param L The Lua VM state.
 * \param cidx The ewindow index.
 * \param s Set or not the ewindow below.
 */
void
ewindow_set_below(lua_State *L, int cidx, bool s)
{
    ewindow_t *ewindow = luaA_checkudata(L, cidx, (lua_class_t *) &ewindow_class);

    if(ewindow->below != s)
    {
        /* You can only be part of one of the special layers. */
        if(s)
        {
            ewindow_set_above(L, cidx, false);
            ewindow_set_ontop(L, cidx, false);
            ewindow_set_fullscreen(L, cidx, false);
        }
        ewindow->below = s;
        luaA_object_emit_signal(L, cidx, "property::below", 0);
    }
}

/** Set a ewindow ontop, or not.
 * \param L The Lua VM state.
 * \param cidx The ewindow index.
 * \param s Set or not the ewindow ontop attribute.
 */
void
ewindow_set_ontop(lua_State *L, int cidx, bool s)
{
    ewindow_t *ewindow = luaA_checkudata(L, cidx, (lua_class_t *) &ewindow_class);

    if(ewindow->ontop != s)
    {
        /* You can only be part of one of the special layers. */
        if(s)
        {
            ewindow_set_above(L, cidx, false);
            ewindow_set_below(L, cidx, false);
            ewindow_set_fullscreen(L, cidx, false);
        }
        ewindow->ontop = s;
        luaA_object_emit_signal(L, cidx, "property::ontop", 0);
    }
}

/** Set an ewindow opacity.
 * \param L The Lua VM state.
 * \param idx The index of the ewindow on the stack.
 * \param opacity The opacity value.
 */
void
ewindow_set_opacity(lua_State *L, int idx, double opacity)
{
    ewindow_t *ewindow = luaA_checkudata(L, idx, (lua_class_t *) &ewindow_class);

    if(ewindow->opacity != opacity)
    {
        ewindow->opacity = opacity;
        xwindow_set_opacity(ewindow->window, opacity);
        luaA_object_emit_signal(L, idx, "property::opacity", 0);
    }
}

/** Set an ewindow opacity.
 * \param L The Lua VM state.
 * \param ewindow The ewindow object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_set_opacity(lua_State *L, ewindow_t *ewindow)
{
    if(lua_isnil(L, -1))
        ewindow_set_opacity(L, -3, -1);
    else
    {
        double d = luaL_checknumber(L, -1);
        if(d >= 0 && d <= 1)
            ewindow_set_opacity(L, -3, d);
    }
    return 0;
}

/** Get the ewindow opacity.
 * \param L The Lua VM state.
 * \param ewindow The ewindow object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_get_opacity(lua_State *L, ewindow_t *ewindow)
{
    if(ewindow->opacity >= 0)
    {
        lua_pushnumber(L, ewindow->opacity);
        return 1;
    }
    return 0;
}

/** Set the ewindow border color.
 * \param L The Lua VM state.
 * \param ewindow The ewindow object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_set_border_color(lua_State *L, ewindow_t *ewindow)
{
    size_t len;
    const char *color_name = luaL_checklstring(L, -1, &len);

    if(color_name &&
       xcolor_init_reply(xcolor_init_unchecked(&ewindow->border_color, color_name, len)))
    {
        xwindow_set_border_color(ewindow->window, &ewindow->border_color);
        luaA_object_emit_signal(L, -3, "property::border_color", 0);
    }

    return 0;
}

/** Set an ewindow border width.
 * \param L The Lua VM state.
 * \param idx The ewindow index.
 * \param width The border width.
 */
void
ewindow_set_border_width(lua_State *L, int idx, int width)
{
    ewindow_t *ewindow = luaA_checkudata(L, idx, (lua_class_t *) &ewindow_class);

    if(width == ewindow->border_width || width < 0)
        return;

    xcb_configure_window(globalconf.connection, ewindow->window,
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         (uint32_t[]) { width });

    ewindow->border_width = width;

    luaA_object_emit_signal(L, idx, "property::border_width", 0);
}

static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, border_width, luaL_checknumber)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, ontop, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, below, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, above, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, minimized, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, maximized_vertical, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, maximized_horizontal, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, modal, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, fullscreen, luaA_checkboolean)

/** Access or set the ewindow tags.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \lparam A table with tags to set, or none to get the current tags table.
 * \return The ewindow tag.
 */
static int
luaA_ewindow_tags(lua_State *L)
{
    ewindow_t *c = luaA_checkudata(L, 1, (lua_class_t *) (lua_class_t *) &ewindow_class);

    if(lua_gettop(L) == 2)
    {
        luaA_checktable(L, 2);
        foreach(tag, c->tags)
        {
            luaA_object_push_item(L, 1, *tag);
            untag_ewindow(L, 1, -1);
            /* remove tag */
            lua_pop(L, 1);
        }
        lua_pushnil(L);
        while(lua_next(L, 2))
        {
            tag_ewindow(L, 1, -1);
            /* remove value (tag) */
            lua_pop(L, 1);
        }
    }

    int i = 0;
    lua_createtable(L, c->tags.len, 0);
    foreach(tag, c->tags)
    {
        luaA_object_push_item(L, 1, *tag);
        lua_rawseti(L, -2, ++i);
    }

    return 1;
}

LUA_OBJECT_DO_SET_PROPERTY_FUNC(ewindow, (lua_class_t *) &ewindow_class, ewindow_t, sticky)
LUA_OBJECT_DO_SET_PROPERTY_FUNC(ewindow, (lua_class_t *) &ewindow_class, ewindow_t, modal)

static int
luaA_ewindow_set_sticky(lua_State *L, ewindow_t *c)
{
    ewindow_set_sticky(L, -3, luaA_checkboolean(L, -1));
    return 0;
}

static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, border_color, luaA_pushxcolor)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, border_width, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, sticky, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, minimized, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, fullscreen, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, modal, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, ontop, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, above, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, below, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, maximized_horizontal, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, maximized_vertical, lua_pushboolean)

void
ewindow_class_setup(lua_State *L)
{
    static const struct luaL_reg ewindow_methods[] =
    {
        LUA_CLASS_METHODS(ewindow)
        { "struts", luaA_ewindow_struts },
        { "tags", luaA_ewindow_tags },
        { NULL, NULL }
    };

    luaA_class_setup(L, (lua_class_t *) &ewindow_class, "ewindow", &window_class,
                     NULL, NULL, NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     ewindow_methods, NULL, NULL);

    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_OPACITY,
                            (lua_class_propfunc_t) luaA_ewindow_set_opacity,
                            (lua_class_propfunc_t) luaA_ewindow_get_opacity,
                            (lua_class_propfunc_t) luaA_ewindow_set_opacity);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_BORDER_COLOR,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_color,
                            (lua_class_propfunc_t) luaA_ewindow_get_border_color,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_color);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_BORDER_WIDTH,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_width,
                            (lua_class_propfunc_t) luaA_ewindow_get_border_width,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_width);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_STICKY,
                            (lua_class_propfunc_t) luaA_ewindow_set_sticky,
                            (lua_class_propfunc_t) luaA_ewindow_get_sticky,
                            (lua_class_propfunc_t) luaA_ewindow_set_sticky);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_ONTOP,
                            (lua_class_propfunc_t) luaA_ewindow_set_ontop,
                            (lua_class_propfunc_t) luaA_ewindow_get_ontop,
                            (lua_class_propfunc_t) luaA_ewindow_set_ontop);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_ABOVE,
                            (lua_class_propfunc_t) luaA_ewindow_set_above,
                            (lua_class_propfunc_t) luaA_ewindow_get_above,
                            (lua_class_propfunc_t) luaA_ewindow_set_above);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_BELOW,
                            (lua_class_propfunc_t) luaA_ewindow_set_below,
                            (lua_class_propfunc_t) luaA_ewindow_get_below,
                            (lua_class_propfunc_t) luaA_ewindow_set_below);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_MINIMIZED,
                            (lua_class_propfunc_t) luaA_ewindow_set_minimized,
                            (lua_class_propfunc_t) luaA_ewindow_get_minimized,
                            (lua_class_propfunc_t) luaA_ewindow_set_minimized);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_FULLSCREEN,
                            (lua_class_propfunc_t) luaA_ewindow_set_fullscreen,
                            (lua_class_propfunc_t) luaA_ewindow_get_fullscreen,
                            (lua_class_propfunc_t) luaA_ewindow_set_fullscreen);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_MODAL,
                            (lua_class_propfunc_t) luaA_ewindow_set_modal,
                            (lua_class_propfunc_t) luaA_ewindow_get_modal,
                            (lua_class_propfunc_t) luaA_ewindow_set_modal);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_MAXIMIZED_HORIZONTAL,
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_horizontal,
                            (lua_class_propfunc_t) luaA_ewindow_get_maximized_horizontal,
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_horizontal);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_MAXIMIZED_VERTICAL,
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_vertical,
                            (lua_class_propfunc_t) luaA_ewindow_get_maximized_vertical,
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_vertical);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80