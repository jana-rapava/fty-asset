/*  =========================================================================
    dbhelpers - Helpers functions for database

    Copyright (C) 2014 - 2015 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/*
@header
    dbhelpers - Helpers functions for database
@discuss
@end
*/

#include "fty_asset_classes.h"
#define INPUT_POWER_CHAIN     1

// for test purposes
std::map<std::string, std::string> test_map_asset_state;

/**
 *  \brief Converts asset name to the DB id
 *
 *  \param[in] name - name of the asset to convert
 *  \param[out] id - converted id
 *
 *  \return  0 - in case of success
 *          -2 - in case if asset was not found
 *          -1 - in case of some unexpected error
 */
int
    select_asset_id (
        const std::string &name,
        a_elmnt_id_t &id
    )
{
    try {
        tntdb::Connection conn = tntdb::connectCached (url);
        tntdb::Statement st = conn.prepareCached(
            " SELECT "
            "   v.id "
            " FROM "
            "   v_bios_asset_element v "
            " WHERE "
            "   v.name = :name "
        );

        tntdb::Row row = st.set("name", name).
                            selectRow();
        row["id"].get(id);
        return 0;
    }
    catch (const tntdb::NotFound &e) {
        zsys_debug ("Requested element not found");
        id = 0;
        return -2;
    }
    catch (const std::exception& e) {
        zsys_error ("Error: ",e.what());
        id = 0;
        return -1;
    }
}

/**
 * \brief Creates condition for type/subtype filtering
 *
 * \param[in] types_and_subtypes - types and subtypes we are interested in
 */
static std::string
select_assets_by_container_filter (
    const std::set<std::string> &types_and_subtypes
)
{
    std::string types, subtypes, filter;

    for (const auto &i: types_and_subtypes) {
        uint32_t t = subtype_to_subtypeid (i);
        if (t != asset_subtype::SUNKNOWN) {
            subtypes +=  "," + std::to_string (t);
        } else {
            t = type_to_typeid (i);
            if (t == asset_type::TUNKNOWN) {
                throw std::invalid_argument ("'" + i + "' is not known type or subtype ");
            }
            types += "," + std::to_string (t);
        }
    }
    if (!types.empty ()) types = types.substr(1);
    if (!subtypes.empty ()) subtypes = subtypes.substr(1);
    if (!types.empty () || !subtypes.empty () ) {
        if (!types.empty ()) {
            filter += " id_type in (" + types + ") ";
            if (!subtypes.empty () ) filter += " OR ";
        }
        if (!subtypes.empty ()) {
            filter += " id_asset_device_type in (" + subtypes + ") ";
        }
    }
    zsys_debug ("filter: '%s'", filter.c_str ());
    return filter;
}

/**
 *  \brief Selects all assets which have our container in input power chain
 *
 *  \param[in] element_id - DB id of container
 *  \param[out] assets - <src,dst> pairs for such assets
 *
 *  \return  0 - in case of success (even if nothing was found)
 *          -1 - in case of some unexpected error
 */
int
    select_links_by_container (
        a_elmnt_id_t element_id,
        std::set <std::pair<a_elmnt_id_t ,a_elmnt_id_t> > &links
    )
{
    links.clear();
    zsys_debug ("  links are selected for element_id = %" PRIi32, element_id);
    uint8_t linktype = INPUT_POWER_CHAIN;

    //      all powerlinks are included into "resultpowers"
    try{
        // v_bios_asset_link are only devices,
        // so there is no need to add more constrains
        tntdb::Connection conn = tntdb::connectCached (url);
        tntdb::Statement st = conn.prepareCached(
            " SELECT"
            "   v.id_asset_element_src,"
            "   v.id_asset_element_dest"
            " FROM"
            "   v_bios_asset_link AS v,"
            "   v_bios_asset_element_super_parent AS v1,"
            "   v_bios_asset_element_super_parent AS v2"
            " WHERE"
            "   v.id_asset_link_type = :linktypeid AND"
            "   v.id_asset_element_dest = v2.id_asset_element AND"
            "   v.id_asset_element_src = v1.id_asset_element AND"
            "   ("
            "       ( :containerid IN (v2.id_parent1, v2.id_parent2 ,v2.id_parent3,"
            "                          v2.id_parent4, v2.id_parent5, v2.id_parent6,"
            "                          v2.id_parent7, v2.id_parent8, v2.id_parent9,"
            "                          v2.id_parent10) ) OR"
            "       ( :containerid IN (v1.id_parent1, v1.id_parent2 ,v1.id_parent3,"
            "                          v1.id_parent4, v1.id_parent5, v1.id_parent6,"
            "                          v1.id_parent7, v1.id_parent8, v1.id_parent9,"
            "                          v1.id_parent10) )"
            "   )"
        );

        // can return more than one row
        tntdb::Result result = st.set("containerid", element_id).
                                  set("linktypeid", linktype).
                                  select();
        zsys_debug("[t_bios_asset_link]: were selected %" PRIu32 " rows",
                                                         result.size());

        // Go through the selected links
        for ( auto &row: result )
        {
            // id_asset_element_src, required
            a_elmnt_id_t id_asset_element_src = 0;
            row[0].get(id_asset_element_src);
            assert ( id_asset_element_src );

            // id_asset_element_dest, required
            a_elmnt_id_t id_asset_element_dest = 0;
            row[1].get(id_asset_element_dest);
            assert ( id_asset_element_dest );

            links.insert(std::pair<a_elmnt_id_t ,a_elmnt_id_t>(id_asset_element_src, id_asset_element_dest));
        } // end for
        return 0;
    }
    catch (const std::exception &e) {
        zsys_error (e.what());
        return -1;
    }
}

/**
 *  \brief Selects assets in a given container
 *
 *  \param[in] element_id - DB id of container
 *  \param[in] types_and_subtypes - types and subtypes we are interested in
 *  \param[in] cb - function to call on each row
 *
 *  \return  0 - in case of success (even if nothing was found)
 *          -1 - in case of some unexpected error
 */
 int
    select_assets_by_container_cb (
        a_elmnt_id_t element_id,
        const std::set<std::string> &types_and_subtypes,
        std::function<void(const tntdb::Row&)> cb
    )
{
    zsys_debug ("container element_id = %" PRIu32, element_id);

    try {
        tntdb::Connection conn = tntdb::connectCached (url);
        std::string request =
            " SELECT "
            "   v.name, "
            "   v.id_asset_element as asset_id, "
            "   v.id_asset_device_type as subtype_id, "
            "   v.type_name as subtype_name, "
            "   v.id_type as type_id "
            " FROM "
            "   v_bios_asset_element_super_parent v "
            " WHERE "
            "   (:containerid in (v.id_parent1, v.id_parent2, v.id_parent3, "
            "                     v.id_parent4, v.id_parent5, v.id_parent6, "
            "                     v.id_parent7, v.id_parent8, v.id_parent9, "
            "                     v.id_parent10) OR :containerid = 0 ) ";

        if(!types_and_subtypes.empty())
            request += " AND ( " + select_assets_by_container_filter (types_and_subtypes) +")";
        zsys_debug("[v_bios_asset_element_super_parent]: %s", request.c_str());

        // Can return more than one row.
        tntdb::Statement st = conn.prepareCached(request);

        tntdb::Result result = st.set("containerid", element_id).
                                  select();
        zsys_debug("[v_bios_asset_element_super_parent]: were selected %" PRIu32 " rows",
                                                            result.size());
        for ( auto &row: result ) {
            cb(row);
        }
        return 0;
    }
    catch (const std::exception& e) {
        zsys_error ("Error: ",e.what());
        return -1;
    }
}

/**
 * \brief Selects all assets in a given container without type/subtype filtering.
 *  Wrapper for select_assets_by_container_cb with 3 arguments
 */
int
    select_assets_by_container_cb (
        a_elmnt_id_t element_id,
        std::function<void(const tntdb::Row&)> cb
    )
{

    std::set <std::string> empty;
    return select_assets_by_container_cb (element_id, empty, cb);
}

/**
 *  \brief Wrapper for select_assets_by_container_cb
 *
 *  \param[in] container_name - iname of container
 *  \param[in] filter - types and subtypes we are interested in
 *  \param[out] assets - inames of assets in this container
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
select_assets_by_container (
        const std::string& container_name,
        const std::set <std::string>& filter,
        std::vector <std::string>& assets,
        bool test)
{
    if (test)
        return 0;

    a_elmnt_id_t id = 0;

    // get container asset id
    if (! container_name.empty ()) {
        if (select_asset_id (container_name, id) != 0) {
            return -1;
        }
    }

    std::function<void(const tntdb::Row&)> func =
        [&assets](const tntdb::Row& row)
        {
            std::string name;
            row["name"].get (name);
            assets.push_back (name);
        };

    return select_assets_by_container_cb (id, filter, func);
}

/**
 *  \brief Selects basic asset info
 *
 *  \param[in] element_id - iname of asset
 *  \param[in] cb - function to call on each row
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
    select_asset_element_basic
        (const std::string &asset_name,
         std::function<void(const tntdb::Row&)> cb,
         bool test,
         std::string status)
{
    if (test)
        return 0;
    zsys_debug ("asset_name = %s", asset_name.c_str());
    tntdb::Connection conn;
    try {
        conn = tntdb::connectCached (url);
    }
    catch ( const std::exception &e) {
        zsys_error ("DB: cannot connect, %s", e.what());
        return -1;
    }

    try{
        tntdb::Statement st = conn.prepareCached(
            " SELECT"
            "   v.id, v.name, v.id_type, v.type_name,"
            "   v.subtype_id, v.id_parent,"
            "   v.id_parent_type, v.status,"
            "   v.priority, v.asset_tag, v.parent_name "
            " FROM"
            "   v_web_element v"
            " WHERE :name = v.name AND "
            " v.status = :vstatus      "
        );

        tntdb::Row row = st.set ("name", asset_name).
                            set ("vstatus", status).
                            selectRow();
        zsys_debug("[v_web_element]: were selected %" PRIu32 " rows", 1);
        cb(row);
        return 0;
    }
    catch (const tntdb::NotFound &e) {
        zsys_info ("end: %s %s", "asset '%s' not found", status.c_str(), asset_name.c_str());
        return -1;
    }
    catch (const std::exception &e) {
        zsys_error ("Cannot select basic asset info: %s", e.what());
        return -1;
    }
}

/**
 *  \brief Selects ext attributes for given asset
 *
 *  \param[in] element_id - iname of asset
 *  \param[in] cb - function to call on each row
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
    select_ext_attributes
        (uint32_t asset_id,
         std::function<void(const tntdb::Row&)> cb,
         bool test)
{
    if (test)
        return 0;
    tntdb::Connection conn;
    try {
        conn = tntdb::connectCached (url);
    }
    catch ( const std::exception &e) {
        zsys_error ("DB: cannot connect, %s", e.what());
        return -1;
    }
    try {
        // Can return more than one row
        tntdb::Statement st_extattr = conn.prepareCached(
            " SELECT"
            "   v.keytag, v.value, v.read_only"
            " FROM"
            "   v_bios_asset_ext_attributes v"
            " WHERE v.id_asset_element = :asset_id"
        );

        tntdb::Result result = st_extattr.set("asset_id", asset_id).
                                          select();
        zsys_debug("[v_bios_asset_ext_attributes]: were selected %" PRIu32 " rows", result.size());

        // Go through the selected extra attributes
        for ( const auto &row: result ) {
            cb(row);
        }
        return 0;
    }
    catch (const std::exception &e) {
        zsys_error ("select_ext: %s", e.what());
        return -1;
    }
}

/**
 *  \brief Selects all parents of given asset
 *
 *  \param[in] id - iname of asset
 *  \param[in] cb - function to call on each row
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
    select_asset_element_super_parent (
            uint32_t id,
            std::function<void(
                const tntdb::Row&
                )>& cb,
            bool test)
{
    if (test)
        return 0;
    tntdb::Connection conn;
    try {
        conn = tntdb::connectCached (url);
    }
    catch ( const std::exception &e) {
        zsys_error ("DB: cannot connect, %s", e.what());
        return -1;
    }
    try{
        tntdb::Statement st = conn.prepareCached(
            " SELECT "
            "   v.id_asset_element as id, "
            "   v.id_parent1 as id_parent1, "
            "   v.id_parent2 as id_parent2, "
            "   v.id_parent3 as id_parent3, "
            "   v.id_parent4 as id_parent4, "
            "   v.id_parent5 as id_parent5, "
            "   v.id_parent6 as id_parent6, "
            "   v.id_parent7 as id_parent7, "
            "   v.id_parent8 as id_parent8, "
            "   v.id_parent9 as id_parent9, "
            "   v.id_parent10 as id_parent10, "
            "   v.name_parent1 as parent_name1, "
            "   v.name_parent2 as parent_name2, "
            "   v.name_parent3 as parent_name3, "
            "   v.name_parent4 as parent_name4, "
            "   v.name_parent5 as parent_name5, "
            "   v.name_parent6 as parent_name6, "
            "   v.name_parent7 as parent_name7, "
            "   v.name_parent8 as parent_name8, "
            "   v.name_parent9 as parent_name9, "
            "   v.name_parent10 as parent_name10, "
            "   v.name as name, "
            "   v.type_name as type_name, "
            "   v.id_asset_device_type as device_type, "
            "   v.status as status, "
            "   v.asset_tag as asset_tag, "
            "   v.priority as priority, "
            "   v.id_type as id_type "
            " FROM v_bios_asset_element_super_parent v "
            " WHERE "
            "   v.id_asset_element = :id "
            );

        tntdb::Result res = st.set ("id", id).select ();
        zsys_debug("[v_bios_asset_element_super_parent]: were selected %i rows", 1);

        for (const auto& r: res) {
            cb(r);
        }
        return 0;
    }
    catch (const std::exception &e) {
        zsys_error ("[v_bios_asset_element_super_parent]: error '%s'", e.what());
        return -1;
    }
}

/**
 *  \brief Selects all assets in the DB of given types/subtypes
 *
 *  \param[in] types_and_subtypes - types/subtypes we are interested in
 *  \param[in] cb - function to call on each row
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
    select_assets_by_filter_cb (
        const std::set<std::string> &types_and_subtypes,
        std::function<void(const tntdb::Row&)> cb
    )
{
    try {
        tntdb::Connection conn = tntdb::connectCached (url);
        std::string request =
            " SELECT "
            "   v.name, "
            "   v.id_asset_element as asset_id, "
            "   v.id_asset_device_type as subtype_id, "
            "   v.type_name as subtype_name, "
            "   v.id_type as type_id "
            " FROM "
            "   v_bios_asset_element_super_parent v ";

        if(!types_and_subtypes.empty())
            request += " WHERE " + select_assets_by_container_filter (types_and_subtypes);
        zsys_debug("[v_bios_asset_element_super_parent]: %s", request.c_str());
        // Can return more than one row.
        tntdb::Statement st = conn.prepareCached(request);
        tntdb::Result result = st.select();
        zsys_debug("[v_bios_asset_element_super_parent]: were selected %" PRIu32 " rows",
                                                            result.size());
        for ( auto &row: result ) {
            cb(row);
        }
        return 0;
    }
    catch (const std::exception& e) {
        zsys_error ("Error: ",e.what());
        return -1;
    }
}

/**
 * Wrapper for select_assets_by_filter_cb
 *
 * \param[in] filter - types/subtypes we are interested in
 * \param[out] assets - inames of returned assets
 */
int
select_assets_by_filter (
        const std::set <std::string>& filter,
        std::vector <std::string>& assets,
        bool test)
{
    if (test)
        return 0;

    std::function<void(const tntdb::Row&)> func =
        [&assets](const tntdb::Row& row)
        {
            std::string name;
            row["name"].get (name);
            assets.push_back (name);
        };

    return select_assets_by_filter_cb (filter, func);
}

/**
 *  \brief Selects basic asset info for all assets in the DB
 *
 *  \param[in] cb - function to call on each row
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
    select_assets (
            std::function<void(
                const tntdb::Row&
                   )>& cb, bool test,
            std::string status)
{
    if (test)
        return 0;
    tntdb::Connection conn;
    try {
        conn = tntdb::connectCached (url);
    }
    catch ( const std::exception &e) {
        zsys_error ("DB: cannot connect, %s", e.what());
        return -1;
    }
    try{
        tntdb::Statement st = conn.prepareCached(
            " SELECT "
            "   v.name, "
            "   v.id,  "
            "   v.id_type,  "
            "   v.id_subtype,  "
            "   v.id_parent,  "
            "   v.parent_name,  "
            "   v.status,  "
            "   v.priority,  "
            "   v.asset_tag  "
            " FROM v_bios_asset_element v  "
            " WHERE v.status=:vstatus"
            );

        tntdb::Result res = st.set ("vstatus", status).select ();
        zsys_debug("[v_bios_asset_element]: were selected %zu rows", res.size());

        for (const auto& r: res) {
            cb(r);
        }
        return 0;
    }
    catch (const tntdb::NotFound &e) {
        zsys_debug("[v_bios_asset_element]: %s asset not found", status.c_str ());
        return -1;
    }
    catch (const std::exception &e) {
        zsys_error ("[v_bios_asset_element]: error '%s'", e.what());
        return -1;
    }
}

#define SQL_EXT_ATT_INVENTORY " INSERT INTO" \
        "   t_bios_asset_ext_attributes" \
        "   (keytag, value, id_asset_element, read_only)" \
        " VALUES" \
        "  ( :keytag, :value, (SELECT id_asset_element FROM t_bios_asset_element WHERE name=:device_name), :readonly)" \
        " ON DUPLICATE KEY" \
        "   UPDATE " \
        "       value = VALUES (value)," \
        "       read_only = :readonly," \
        "       id_asset_ext_attribute = LAST_INSERT_ID(id_asset_ext_attribute)"
/**
 *  \brief Inserts ext attributes from inventory message into DB
 *
 *  \param[in] device_name - iname of assets
 *  \param[in] ext_attributes - recent ext attributes for this asset
 *  \param[in] read_only - whether to insert ext attributes as readonly
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
process_insert_inventory
    (const std::string& device_name,
    zhash_t *ext_attributes,
    bool readonly,
    bool test)
{
    if (test)
        return 0;
    tntdb::Connection conn;
    try {
        conn = tntdb::connectCached (url);
    }
    catch ( const std::exception &e) {
        zsys_error ("DB: cannot connect, %s", e.what());
        return -1;
    }

    tntdb::Transaction trans (conn);
    tntdb::Statement st = conn.prepareCached (SQL_EXT_ATT_INVENTORY);

    for (void* it = zhash_first (ext_attributes);
               it != NULL;
               it = zhash_next (ext_attributes)) {

        const char *value = (const char*) it;
        const char *keytag = (const char*)  zhash_cursor (ext_attributes);
        bool readonlyV = readonly;

        if(strcmp(keytag, "name") == 0 || strcmp(keytag, "description") == 0 || strcmp(keytag, "ip.1") == 0) {
            readonlyV = false;
        }
        if (strcmp(keytag, "uuid") == 0) {
            readonlyV = true;
        }
        try {
            st.set ("keytag", keytag).
               set ("value", value).
               set ("device_name", device_name).
               set ("readonly", readonlyV).
               execute ();
        }
        catch (const std::exception &e)
        {
            zsys_warning ("%s:\texception on updating %s {%s, %s}\n\t%s", "", device_name.c_str (), keytag, value, e.what ());
            continue;
        }
    }

    trans.commit ();
    return 0;
}

/**
 *  \brief Inserts ext attributes from inventory message into DB only
 *         if the new value is different from the cache map
 *         This method is not thread safe.
 *
 *  \param[in] device_name - iname of assets
 *  \param[in] ext_attributes - recent ext attributes for this asset
 *  \param[in] read_only - whether to insert ext attributes as readonly
 *  \param[in] test - unit tests indicator
 *  \param[in] map_cache -  cache map
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
process_insert_inventory
    (const std::string& device_name,
    zhash_t *ext_attributes,
    bool readonly,
    std::unordered_map<std::string,std::string> &map_cache,
    bool test)
 {
    if (test)
       return 0;
    tntdb::Connection conn;
    try {
        conn = tntdb::connectCached (url);
    }
    catch ( const std::exception &e) {
        zsys_error ("DB: cannot connect, %s", e.what());
       return -1;
    }

    tntdb::Transaction trans (conn);
    tntdb::Statement st = conn.prepareCached (SQL_EXT_ATT_INVENTORY);

    for (void* it = zhash_first (ext_attributes);
               it != NULL;
               it = zhash_next (ext_attributes)) {
        const char *value = (const char*) it;
        const char *keytag = (const char*)  zhash_cursor (ext_attributes);
        bool readonlyV = readonly;
        if(strcmp(keytag, "name") == 0 || strcmp(keytag, "description") == 0)
            readonlyV = false;

        std::string cache_key = device_name;
        cache_key.append(":").append(keytag).append(readonlyV ? "1" : "0");
        auto el = map_cache.find(cache_key);
        if (el != map_cache.end() && el->second == value)
            continue;
        try {
            st.set ("keytag", keytag).
               set ("value", value).
               set ("device_name", device_name).
               set ("readonly", readonlyV).
               execute ();
            map_cache[cache_key]=value;
        }
       catch (const std::exception &e)
        {
            zsys_warning ("%s:\texception on updating %s {%s, %s}\n\t%s", "", device_name.c_str (), keytag, value, e.what ());
            continue;
        }
    }

    trans.commit ();
    return 0;
}
/**
 *  \brief Selects user-friendly name for given asset name
 *
 *  \param[in] iname - asset iname
 *  \param[out] ename - user-friendly asset name
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
int
select_ename_from_iname
    (std::string &iname,
     std::string &ename,
     bool test)
{
    if (test) {
        if (iname == TEST_INAME) {
            ename = TEST_ENAME;
            return 0;
        }
        else
            return -1;
    }
    try
    {

        tntdb::Connection conn = tntdb::connectCached (url);
        tntdb::Statement st = conn.prepareCached (
            "SELECT e.value FROM  t_bios_asset_ext_attributes AS e "
            "INNER JOIN t_bios_asset_element AS a "
            "ON a.id_asset_element = e.id_asset_element "
            "WHERE keytag = 'name' and a.name = :iname; "

        );

        tntdb::Row row = st.set ("iname", iname).selectRow ();
        zsys_debug ("[s_handle_subject_ename_from_iname]: were selected %" PRIu32 " rows", 1);

        row [0].get (ename);
    }
    catch (const std::exception &e)
    {
        zsys_error ("exception caught %s for element '%s'", e.what (), ename.c_str ());
        return -1;
    }

    return 0;
}


/**
 *  \brief Disable power nodes above licensing limit
 *
 *  \return  void
 *
 *  \throws  database errors
 */

bool disable_power_nodes_if_limitation_applies (int max_active_power_devices, bool test)
{
    if (0 >= max_active_power_devices) {
        // limitations disabled
        return false;
    }
    if (test) {
        zsys_debug ("[disable_power_nodes_if_limitation_applies]: runs in test mode");
        return false;
    }
    try
    {
        int active_power_devices = get_active_power_devices (test);
        if ( active_power_devices > max_active_power_devices) {
            // need to limit amount of active power devices
            tntdb::Connection conn = tntdb::connectCached (url);
            // this query would have been much easier if only MySQL supported LIMIT and IN in one query
            tntdb::Statement statement = conn.prepareCached (
                "UPDATE t_bios_asset_element "
                "SET status='nonactive' "
                "WHERE id_asset_element IN "
                "("
                    "SELECT id_asset_element "
                    "FROM "
                    "("
                        "SELECT id_asset_element "
                        "FROM t_bios_asset_element "
                        "WHERE "
                        "status = 'active' AND "
                        "( "
                            "id_subtype = (SELECT id_asset_device_type "
                                "FROM t_bios_asset_device_type "
                                "WHERE name = 'epdu' LIMIT 1) "
                            "OR id_subtype = (SELECT id_asset_device_type "
                                "FROM t_bios_asset_device_type "
                                "WHERE name = 'sts' LIMIT 1) "
                            "OR id_subtype = (SELECT id_asset_device_type "
                                "FROM t_bios_asset_device_type "
                                "WHERE name = 'ups' LIMIT 1) "
                            "OR id_subtype = (SELECT id_asset_device_type "
                                "FROM t_bios_asset_device_type "
                                "WHERE name = 'pdu' LIMIT 1) "
                            "OR id_subtype = (SELECT id_asset_device_type "
                                "FROM t_bios_asset_device_type "
                                "WHERE name = 'genset' LIMIT 1) "
                        ")"
                        "ORDER BY id_asset_element ASC "
                        "LIMIT 18446744073709551615 OFFSET :max_active_power_devices "
                    ") "
                    "AS res "
                "); "
            );
            statement.set ("max_active_power_devices", max_active_power_devices).execute ();
            return true;
        }
    }
    catch (const std::exception &e)
    {
        zsys_error ("[disable_power_nodes_if_limitation_applies]: exception caught %s when getting count of active power devices", e.what ());
        return false;
    }
    return false;
}


/**
 *  \brief Get number of active power devices
 *
 *  \return  X - number of active power devices
 */

int
get_active_power_devices (bool test)
{
    int count = 0;
    if (test) {
        zsys_debug ("[get_active_power_devices]: runs in test mode");
        for (auto const& as : test_map_asset_state) {
            if ("active" == as.second) {
                ++count;
            }
        }
        return count;
    }
    try
    {
        tntdb::Connection conn = tntdb::connectCached (url);
        tntdb::Statement st = conn.prepareCached (
            "SELECT COUNT(*) AS CNT FROM t_bios_asset_element "
            "WHERE id_subtype IN "
                "(SELECT id_asset_device_type FROM t_bios_asset_device_type "
                "WHERE name IN ('epdu', 'sts', 'ups', 'pdu', 'genset')) "
            "AND status = 'active'; "
        );

        tntdb::Row row = st.selectRow ();
        zsys_debug ("[get_active_power_devices]: were selected %" PRIu32 " rows", 1);

        row [0].get (count);
    }
    catch (const std::exception &e)
    {
        zsys_error ("[get_active_power_devices]: exception caught %s when getting count of active power devices", e.what ());
        return 0;
    }

    return count;
}

/**
 *  \brief Inserts data from create/update message into DB
 *
 *  \param[in] fmsg - create/update message
 *  \param[in] read_only - whether to insert ext attributes as readonly
 *  \param[in] test - unit tests indicator
 *
 *  \return  0 - in case of success
 *          -1 - in case of some unexpected error
 */
db_reply_t
    create_or_update_asset (fty_proto_t *fmsg, bool read_only, bool test, LIMITATIONS_STRUCT *limitations)
{
    const char   *element_name;
    uint64_t      type_id;
    unsigned int  subtype_id;
    uint64_t      parent_id;
    const char   *status;
    unsigned int  priority;
    const char   *operation;
    bool          update;

    db_reply_t ret = db_reply_new ();

    if (0 == limitations->global_configurability) {
        ret.status     = -1;
        ret.errtype    = LICENSING_ERR;
        ret.errsubtype = LICENSING_GLOBAL_CONFIGURABILITY_DISABLED;
        return ret;
    }
    type_id = type_to_typeid (fty_proto_aux_string (fmsg, "type", ""));
    subtype_id = subtype_to_subtypeid (fty_proto_aux_string (fmsg, "subtype", ""));
    if (subtype_id == 0) subtype_id = asset_subtype::N_A;
    parent_id = fty_proto_aux_number (fmsg, "parent", 0);
    status = fty_proto_aux_string (fmsg, "status", "nonactive");
    if (limitations->max_active_power_devices >= 0 && type_id == asset_type::DEVICE && streq (status, "active")) {
        switch (subtype_id) {
            default:
                // no default, not to generate warning
                break;
            case asset_subtype::PDU:
            case asset_subtype::GENSET:
            case asset_subtype::EPDU:
            case asset_subtype::UPS:
            case asset_subtype::STS:
                // check if power devices exceeded allowed limit
                int pd_active = get_active_power_devices(test);
                if (pd_active + 1 > limitations->max_active_power_devices) {
                    // raise error if limit reached
                    ret.status     = -1;
                    ret.errtype    = LICENSING_ERR;
                    ret.errsubtype = LICENSING_POWER_DEVICES_COUNT_REACHED;
                    return ret;
                }
                break;
        }
    }
    priority = fty_proto_aux_number (fmsg, "priority", 5);
    element_name = fty_proto_name (fmsg);
    // TODO: element name from ext.name?
    operation = fty_proto_operation (fmsg);
    update = streq (operation, "update");

    if (!update) {
        if (streq (element_name,"")) {
            if (type_id == asset_type::DEVICE) {
                element_name = asset_subtype2str (subtype_id);
            } else {
                element_name = asset_type2str (type_id);
            }
        }
        // TODO: sanitize name ("rack controller")
    }
    zsys_debug ("  element_name = '%s'", element_name);

    // ASSUMPTION: all datacenters are unlocated elements
    if (type_id == asset_type::DATACENTER && parent_id != 0)
    {
        ret.status     = 0;
        ret.errtype    = DB_ERR;
        ret.errsubtype = DB_ERROR_BADINPUT;
        return ret;
    }
    // TODO: check whether asset exists and drop?

    if (test) {
        test_map_asset_state[std::string(element_name)] = std::string(status);
        ret.status = 1;
        return ret;
    }
    try {
        // this concat with last_insert_id may have race condition issue but hopefully is not important
        tntdb::Connection conn = tntdb::connectCached (url);
        tntdb::Statement statement;
        if (update) {
            statement = conn.prepareCached (
                " INSERT INTO t_bios_asset_element "
                " (name, id_type, id_subtype, id_parent, status, priority, asset_tag) "
                " VALUES "
                " (:name, :id_type, :id_subtype, :id_parent, :status, :priority, :asset_tag) "
                " ON DUPLICATE KEY UPDATE name = :name "
            );
        } else {
            // @ is prohibited in name => name-@@-342 is unique
            statement = conn.prepareCached (
                " INSERT INTO t_bios_asset_element "
                " (name, id_type, id_subtype, id_parent, status, priority, asset_tag) "
                " VALUES "
                " (concat (:name, '-@@-', " + std::to_string (rand ())  + "), :id_type, :id_subtype, :id_parent, :status, :priority, :asset_tag) "
            );
        }
        if (parent_id == 0)
        {
            ret.affected_rows = statement.
                set ("name", element_name).
                set ("id_type", type_id).
                set ("id_subtype", subtype_id).
                setNull ("id_parent").
                set ("status", status).
                set ("priority", priority).
                set ("asset_tag", "").
                execute();
        }
        else
        {
            ret.affected_rows = statement.
                set ("name", element_name).
                set ("id_type", type_id).
                set ("id_subtype", subtype_id).
                set ("id_parent", parent_id).
                set ("status", status).
                set ("priority", priority).
                set ("asset_tag", "").
                execute();
        }

        ret.rowid = conn.lastInsertId ();
        zsys_debug ("[t_bios_asset_element]: was inserted %" PRIu64 " rows", ret.affected_rows);
        if (! update) {
            // it is insert, fix the name
            statement = conn.prepareCached (
                " UPDATE t_bios_asset_element "
                "  set name = concat(:name, '-', :id) "
                " WHERE id_asset_element = :id "
            );
            statement.set ("name", element_name).
                set ("id", ret.rowid).
                execute();
            // also set name to fty_proto
            fty_proto_set_name (fmsg, "%s-%" PRIu64, element_name, ret.rowid);
        }
        if (ret.affected_rows == 0)
            zsys_debug ("Asset unchanged, processing inventory");
        else
            zsys_debug ("Insert went well, processing inventory.");
        process_insert_inventory (fty_proto_name (fmsg), fty_proto_ext (fmsg), read_only, false);
        ret.status = 1;
        return ret;
    }
    catch (const std::exception &e) {
        ret.status     = 0;
        ret.errtype    = DB_ERR;
        ret.errsubtype = DB_ERROR_INTERNAL;
        // bios_error_idx(ret.rowid, ret.msg, "internal-error", "Unspecified issue with database.");
        return ret;
    }
}

// returns map with desired assets and their ids
db_reply <std::map <uint32_t, std::string> >
    select_short_elements
        (tntdb::Connection &conn,
         uint32_t type_id,
         uint32_t subtype_id,
         std::string status)
{
    zsys_debug ("  type_id = %" PRIi16, type_id);
    zsys_debug ("  subtype_id = %" PRIi16, subtype_id);
    std::map <uint32_t, std::string> item{};
    db_reply <std::map <uint32_t, std::string> > ret = db_reply_new(item);

    std::string query;
    if ( subtype_id == 0 )
    {
        query = " SELECT "
                "   v.name, v.id "
                " FROM "
                "   v_bios_asset_element v "
                " WHERE "
                "   v.id_type = :typeid AND "
                "   v.status = :vstatus";
    }
    else
    {
        query = " SELECT "
                "   v.name, v.id "
                " FROM "
                "   v_bios_asset_element v "
                " WHERE "
                "   v.id_type = :typeid AND "
                "   v.id_subtype = :subtypeid AND"
                "   v.status = :vstatus ";
    }
    try {
        // Can return more than one row.
        tntdb::Statement st = conn.prepareCached(query);

        tntdb::Result result;
        if ( subtype_id == 0 )
        {
            result = st.set ("typeid", type_id).
                        set ("vstatus", status).
                        select ();
        } else {
            result = st.set("typeid", type_id).
                    set("subtypeid", subtype_id).
                    set ("vstatus", status).
                    select();
        }

        // Go through the selected elements
        for (auto const& row: result) {
            std::string name;
            row[0].get(name);
            uint32_t id = 0;
            row[1].get(id);
            ret.item.insert(std::pair<uint32_t, std::string>(id, name));
        }
        ret.status = 1;

        return ret;
    }
    catch (const std::exception &e) {
        ret.status        = 0;
        ret.errtype       = DB_ERR;
        ret.errsubtype    = DB_ERROR_INTERNAL;
        ret.msg           = e.what();
        ret.item.clear();
        zsys_error ("Exception caught %s", e.what());
        return ret;
    }
}

//  Self test of this class
void
dbhelpers_test (bool verbose)
{
    printf (" * dbhelpers: ");

    printf ("OK\n");
}
