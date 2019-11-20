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
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    int rv = DBAssets::select_assets_by_container_name_filter (conn, container_name, filter, assets);
    return rv;
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
         bool test)
{
    if (test)
        return 0;
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    int rv = DBAssets::select_asset_element_basic_cb (conn, asset_name, cb);
    return rv;
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
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    int rv = DBAssets::select_ext_attributes_cb (conn, asset_id, cb);
    return rv;
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
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    int rv = DBAssets::select_asset_element_super_parent (conn, id, cb);
    return rv;
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

    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    int rv = DBAssets::select_assets_by_filter (conn, filter, assets);
    return rv;
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
                )>& cb, bool test)
{
    if (test)
        return 0;
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    int rv = DBAssets::select_assets_cb (conn, cb);
    return rv;
}

////////////////////////////////////////////////////////////////////////////////

/**
 *  \brief Selects status string for selected asset in the DB
 *
 *  \param[in] element_name - name of asset in question
 *
 *  \return <status> - in case of success
 *          "unknown" - in case of failure
 */
static std::string get_status_from_db (std::string element_name, bool test = false) {
    if (test) {
        return "nonactive";
    }
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    return DBAssets::get_status_from_db (conn, element_name);
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
        conn = tntdb::connectCached (DBConn::url);
    }
    catch ( const std::exception &e) {
        log_error ("DB: cannot connect, %s", e.what());
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
            log_warning ("%s:\texception on updating %s {%s, %s}\n\t%s", "", device_name.c_str (), keytag, value, e.what ());
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
 *           2 - in case nothing has changed
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
        conn = tntdb::connectCached (DBConn::url);
    }
    catch ( const std::exception &e) {
        log_error ("DB: cannot connect, %s", e.what());
       return -1;
    }

    tntdb::Transaction trans (conn);
    tntdb::Statement st = conn.prepareCached (SQL_EXT_ATT_INVENTORY);

    bool updated = false;
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
            updated = true;
            st.set ("keytag", keytag).
               set ("value", value).
               set ("device_name", device_name).
               set ("readonly", readonlyV).
               execute ();
            map_cache[cache_key]=value;
        }
       catch (const std::exception &e)
        {
            log_warning ("%s:\texception on updating %s {%s, %s}\n\t%s", "", device_name.c_str (), keytag, value, e.what ());
            continue;
        }
    }

    trans.commit ();
    if (updated)
    {
        return 2;
    }
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

        tntdb::Connection conn = tntdb::connectCached (DBConn::url);
        tntdb::Statement st = conn.prepareCached (
            "SELECT e.value FROM  t_bios_asset_ext_attributes AS e "
            "INNER JOIN t_bios_asset_element AS a "
            "ON a.id_asset_element = e.id_asset_element "
            "WHERE keytag = 'name' and a.name = :iname; "

        );

        tntdb::Row row = st.set ("iname", iname).selectRow ();
        log_debug ("[s_handle_subject_ename_from_iname]: were selected %" PRIu32 " rows", 1);

        row [0].get (ename);
    }
    catch (const std::exception &e)
    {
        log_error ("exception caught %s for element '%s'", e.what (), ename.c_str ());
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
        log_debug ("[disable_power_nodes_if_limitation_applies]: runs in test mode");
        return false;
    }
    try
    {
        int active_power_devices = get_active_power_devices (test);
        if ( active_power_devices > max_active_power_devices) {
            // need to limit amount of active power devices
            tntdb::Connection conn = tntdb::connectCached (DBConn::url);
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
        log_error ("[disable_power_nodes_if_limitation_applies]: exception caught %s when getting count of active power devices", e.what ());
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
        log_debug ("[get_active_power_devices]: runs in test mode");
        for (auto const& as : test_map_asset_state) {
            if ("active" == as.second) {
                ++count;
            }
        }
        return count;
    }
    tntdb::Connection conn = tntdb::connectCached (DBConn::url);
    return DBAssets::get_active_power_devices (conn);
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
    type_id = persist::type_to_typeid (fty_proto_aux_string (fmsg, "type", ""));
    subtype_id = persist::subtype_to_subtypeid (fty_proto_aux_string (fmsg, "subtype", ""));
    if (subtype_id == 0) subtype_id = persist::asset_subtype::N_A;
    parent_id = fty_proto_aux_number (fmsg, "parent", 0);
    status = fty_proto_aux_string (fmsg, "status", "nonactive");
    priority = fty_proto_aux_number (fmsg, "priority", 5);
    std::string element_name (fty_proto_name (fmsg));
    // TODO: element name from ext.name?
    operation = fty_proto_operation (fmsg);
    update = streq (operation, "update");

    if (!update) {
        if (element_name.empty ()) {
            if (type_id == persist::asset_type::DEVICE) {
                element_name = persist::subtypeid_to_subtype (subtype_id);
            } else {
                element_name = persist::typeid_to_type (type_id);
            }
        }
        // TODO: sanitize name ("rack controller")
    }
    log_debug ("  element_name = '%s'", element_name.c_str ());

    if (limitations->max_active_power_devices >= 0 && type_id == persist::asset_type::DEVICE && streq (status, "active")) {
        std::string db_status = get_status_from_db (element_name.c_str (), test);
        // limit applies only to assets that are attempted to be activated, but are disabled in database
        // or to new assets, also may trigger in case of DB failure, but that's fine
        if (db_status != "active") {
            switch (subtype_id) {
                default:
                // no default, not to generate warning
                    break;
                case persist::asset_subtype::PDU:
                case persist::asset_subtype::GENSET:
                case persist::asset_subtype::EPDU:
                case persist::asset_subtype::UPS:
                case persist::asset_subtype::STS:
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
    }

    // ASSUMPTION: all datacenters are unlocated elements
    if (type_id == persist::asset_type::DATACENTER && parent_id != 0)
    {
        ret.status     = 0;
        ret.errtype    = DB_ERR;
        ret.errsubtype = DB_ERROR_BADINPUT;
        return ret;
    }
    // TODO: check whether asset exists and drop?

    if (test) {
        log_debug ("[create_or_update_asset]: runs in test mode");
        test_map_asset_state[std::string(element_name)] = std::string(status);
        ret.status = 1;
        return ret;
    }

    try {
        // this concat with last_insert_id may have race condition issue but hopefully is not important
        tntdb::Connection conn = tntdb::connectCached (DBConn::url);
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
        log_debug ("[t_bios_asset_element]: was inserted %" PRIu64 " rows", ret.affected_rows);
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
            fty_proto_set_name (fmsg, "%s-%" PRIu64, element_name.c_str (), ret.rowid);
        }
        if (ret.affected_rows == 0)
            log_debug ("Asset unchanged, processing inventory");
        else
            log_debug ("Insert went well, processing inventory.");
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

//  Self test of this class
void
dbhelpers_test (bool verbose)
{
    printf (" * dbhelpers: ");

    printf ("OK\n");
}
