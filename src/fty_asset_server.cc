/*  =========================================================================
    fty_asset_server - Asset server, that takes care about distribution of asset information across the system

    Copyright (C) 2014 - 2017 Eaton

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
    fty_asset_server - Asset server, that takes care about distribution of asset information across the system
@discuss
     ASSET PROTOCOL

     ------------------------------------------------------------------------
     ## Topology request

     power topology request:
         subject: "TOPOLOGY"
         message: is a multipart message A/B
                 A = "TOPOLOGY_POWER" - mandatory
                 B = "asset_name" - mandatory

     power topology reply in "OK" case:
         subject: "TOPOLOGY"
         message: is a multipart message A/B/D/C1/.../CN
                 A = "TOPOLOGY_POWER" - mandatory
                 B = "asset_name" - mandatory
                 D = "OK" - mandatory
                 Ci = "asset_name" of power source - not mandatory
                     if there are no power devices
                      -> message is A/B/D

     power topology reply in "ERROR" case:
         subject: "TOPOLOGY"
         message: is a multipart message A/B/D/E
                 A = "TOPOLOGY_POWER" - mandatory
                 B = "asset_name" - mandatory
                 D = "ERROR" - mandatory
                 E = "ASSET_NOT_FOUND"/"INTERNAL_ERROR" - mandatory


    ------------------------------------------------------------------------
    ## Asset manipulation protocol

    REQ:
        subject: "ASSET_MANIPULATION"
        Message is a fty protocol (fty_proto_t) message

        *) read-only/fty_proto ASSET message

        where:
        * 'operation' is one of [ create | update | delete | retire ].
           Asset messages with different operation value are discarded and not replied to.
        * 'read-only' tells us whether ext attributes should be inserted as read-only or not.
           Allowed values are READONLY and READWRITE.

    REP:
        subject: same as in REQ
        Message is a multipart string message:

        * OK/<asset_id>
        * ERROR/<reason>

        where:
 (OPTIONAL)     <asset_id>  = asset id (in case of create, update operation)
                <reason>    = Error message/code TODO

    Note: in REQ message certain asset information are encoded as follows

      'ext' field
          Power Links - key: "power_link.<device_name>", value: "<first_outlet_num>/<second_outlet_num>", i.e. 1 --> 2 == "1/2"
          Groups - key: "group", value: "<group_name_1>/.../<group_name_N>"


    ------------------------------------------------------------------------
    ## ASSETS in container

    REQ:
        subject: "ASSETS_IN_CONTAINER"
        Message is a multipart string message

        * GET/<container name>/<type 1>/.../<type n>

        where:
            <container name>        = Name of the container things belongs to that
                                      when empty, no container is used, so all assets are take in account
            <type X>                = Type or subtype to be returned. Possible values are
                                      ups
                                      TODO: add more
                                      when empty, no filtering is done
    REP:
        subject: "ASSETS_IN_CONTAINER"
        Message is a multipart message:

        * OK                         = empty container
        * OK/<asset 1>/.../<asset N> = non-empty
        * ERROR/<reason>

        where:
            <reason>          = ASSET_NOT_FOUND / INTERNAL_ERROR / BAD_COMMAND

    REQ:
        subject: "ASSETS"
        Message is a multipart string message

        * GET/<uuid>/<type 1>/.../<type n>

        where:
            <uuid>                  = zuuid of  message
            <type X>                = Type or subtype to be returned. Possible values are
                                      ups
                                      TODO: add more
                                      when empty, no filtering is done
    REP:
        subject: "ASSETS"
        Message is a multipart message:

        * OK                         = empty container
        * OK/<uuid>/<asset 1>/.../<asset N> = non-empty
        * ERROR/<uuid>/<reason>

        where:
            <reason>          = ASSET_NOT_FOUND / INTERNAL_ERROR / BAD_COMMAND
    ------------------------------------------------------------------------
    ## REPUBLISH

    REQ:
        subject: "REPUBLISH"
        Message is a multipart string message

        /asset1/asset2/asset3       - republish asset information about asset1 asset2 and asset3
        /$all                       - republish information about all assets

     ------------------------------------------------------------------------
     ## ENAME_FROM_INAME

     request user-friendly name for given iname:
         subject: "ENAME_FROM_INAME"
         message: is a string message A
                 B = "asset_iname" - mandatory

     reply in "OK" case:
         subject: "ENAME_FROM_INAME"
         message: is a multipart message A/B
                 A = "OK" - mandatory
                 B = user-friendly name of given asset - mandatory

     reply in "ERROR" case:
         subject: "ENAME_FROM_INAME"
         message: is a multipart message A/B
                 A = "ERROR" - mandatory
                 B = "ASSET_NOT_FOUND"/"MISSING_INAME" - mandatory


     ------------------------------------------------------------------------
     ## ASSET_DETAIL

     request all the available data about given asset:
         subject: "ASSET_DETAIL"
         message: is a multipart message A/B/C
                 A = "GET" - mandatory
                 B = uuid - mandatory
                 C = "asset_name" - mandatory

     power topology reply in "OK" case:
         subject: "ASSET_DETAIL"
         message: is fty-proto asset update message

     power topology reply in "ERROR" case:
         subject: "ASSET_DETAIL"
         message: is a multipart message A/B
                 A = "ERROR" - mandatory
                 B = "BAD_COMMAND"/"INTERNAL_ERROR"/"ASSET_NOT_FOUND" - mandatory


@end
*/

#include "fty_asset_classes.h"
#include <string>
#include <tntdb/connect.h>
#include <functional>
#include <fty_common_db_uptime.h>
// TODO add dependencies tntdb and cxxtools

//  Structure of our class

struct _fty_asset_server_t {
    char *name;
    mlm_client_t *mailbox_client;
    mlm_client_t *stream_client;
    bool test;
    LIMITATIONS_STRUCT limitations;
};


//  --------------------------------------------------------------------------
//  Destroy the fty_asset_server

void
fty_asset_server_destroy (fty_asset_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        fty_asset_server_t *self = *self_p;
        zstr_free (&self->name);
        mlm_client_destroy (&self->mailbox_client);
        mlm_client_destroy (&self->stream_client);
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Create a new fty_asset_server

fty_asset_server_t *
fty_asset_server_new (void)
{
    fty_asset_server_t *self = (fty_asset_server_t *) zmalloc (sizeof (fty_asset_server_t));
    assert (self);
    self->mailbox_client = mlm_client_new ();
    if (!self->mailbox_client) {
        log_fatal ("mailbox mlm_client failed");
        exit (1);
    }
    self->stream_client = mlm_client_new ();
    if (!self->stream_client) {
        log_fatal ("stream mlm_client failed");
        exit (1);
    }

    self->test = false;
    self->limitations.max_active_power_devices = -1;
    self->limitations.global_configurability = 1;
    return self;
}


// ============================================================
//         Functionality for TOPOLOGY processing
// ============================================================
static void
    s_processTopology(
        fty_asset_server_t *cfg,
        const std::string &assetName)
{
    // result of power topology - list of power device names
    std::vector<std::string> powerDevices{};

    // select power devices
    int rv = select_devices_total_power(assetName, powerDevices, cfg->test);

    // message to be sent as reply
    zmsg_t *msg = zmsg_new ();

    // these 2 parts are the same for OK and ERROR messages
    zmsg_addstr (msg, "TOPOLOGY_POWER");
    zmsg_addstr (msg, assetName.c_str());

    // form a message according the contract for the case "OK" and for the case "ERROR"
    if ( rv == -1 ) {
        log_error ("%s:\tTOPOLOGY_POWER: Cannot select power sources", cfg->name);
        zmsg_addstr (msg, "ERROR");
        zmsg_addstr (msg, "INTERNAL_ERROR");
    } else if ( rv == -2 ) {
        log_debug ("%s:\tTOPOLOGY_POWER: Asset was not found", cfg->name);
        zmsg_addstr (msg, "ERROR");
        zmsg_addstr (msg, "ASSET_NOT_FOUND");
    } else {
        zmsg_addstr (msg, "OK");
        log_debug ("%s:\tPower topology for '%s':", cfg->name, assetName.c_str());
        for (const auto &powerDeviceName : powerDevices ) {
            log_debug ("%s:\t\t%s", cfg->name, powerDeviceName.c_str());
            zmsg_addstr (msg, powerDeviceName.c_str());
        }
    }
    rv = mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "TOPOLOGY", NULL, 5000, &msg);
    if ( rv != 0 )
        log_error ("%s:\tTOPOLOGY_POWER: cannot send response message", cfg->name);
}

static void
    s_handle_subject_topology(
        fty_asset_server_t *cfg,
        zmsg_t *zmessage)
{
    assert (zmessage);
    assert (cfg);
    char* command = zmsg_popstr (zmessage);
    if ( streq (command, "TOPOLOGY_POWER") ) {
        char* asset_name = zmsg_popstr (zmessage);
        s_processTopology (cfg, asset_name);
        zstr_free (&asset_name);
    }
    else
        log_error ("%s:\tUnknown command for subject=TOPOLOGY '%s'", cfg->name, command);
    zstr_free (&command);
}

static void
    s_handle_subject_assets_in_container (
        fty_asset_server_t *cfg,
        zmsg_t *msg)
{
    assert (msg);
    assert (cfg);
    if (zmsg_size (msg) < 2) {
        log_error ("%s:\tASSETS_IN_CONTAINER: incoming message have less than 2 frames", cfg->name);
        return;
    }

    char* c_command = zmsg_popstr (msg);
    zmsg_t *reply = zmsg_new ();

    if (! streq (c_command, "GET")) {
        log_error ("%s:\tASSETS_IN_CONTAINER: bad command '%s', expected GET", cfg->name, c_command);
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "BAD_COMMAND");
    }
    zstr_free (&c_command);

    std::string container_name;
    char* c_container_name = zmsg_popstr (msg);
    container_name = c_container_name;
    zstr_free (&c_container_name);

    std::set <std::string> filters;
    while (zmsg_size (msg) > 0) {
        char *filter = zmsg_popstr (msg);
        filters.insert (filter);
        zstr_free (&filter);
    }
    std::vector <std::string> assets;
    int rv = 0;

    // if there is no error msg prepared, call SQL
    if (zmsg_size (msg) == 0)
            rv = select_assets_by_container (container_name, filters, assets, cfg->test);

    if (rv == -1)
    {
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "INTERNAL_ERROR");
    }
    else
    if (rv == -2)
    {
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "ASSET_NOT_FOUND");
    }
    else
    {
        zmsg_addstr (reply, "OK");
        for (const auto& dev : assets)
            zmsg_addstr (reply, dev.c_str ());
    }

    // send the reply
    rv = mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSETS_IN_CONTAINER", NULL, 5000, &reply);
    if (rv == -1)
        log_error ("%s:\tASSETS_IN_CONTAINER: mlm_client_sendto failed", cfg->name);

}

static void
    s_handle_subject_ename_from_iname(
        fty_asset_server_t *cfg,
        zmsg_t *msg)
{
    zmsg_t *reply = zmsg_new ();
    if (zmsg_size (msg) < 1) {
        log_error ("%s:\tENAME_FROM_INAME: incoming message have less than 1 frame", cfg->name);
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "MISSING_INAME");
        mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ENAME_FROM_INAME", NULL, 5000, &reply);
        return;
    }
    char *iname_str = zmsg_popstr (msg);
    std::string iname (iname_str);
    std::string ename;

    select_ename_from_iname (iname, ename, cfg->test);

    zstr_free (&iname_str);
    if (ename.empty ())
    {
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "ASSET_NOT_FOUND");
    }
    else
    {
        zmsg_addstr (reply, "OK");
        zmsg_addstr (reply, ename.c_str());
    }
    if (-1 == mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ENAME_FROM_INAME", NULL, 5000, &reply))
        log_error ("%s:\tENAME_FROM_INAME: mlm_client_sendto failed", cfg->name);
}

static void
    s_handle_subject_assets (
        fty_asset_server_t *cfg,
        zmsg_t *msg)
{
    zmsg_t *reply = zmsg_new ();
    if (zmsg_size (msg) < 1) {
        log_error ("%s:\tASSETS: incoming message have less than 1 frame", cfg->name);
        zmsg_addstr (reply, "0");
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "MISSING_COMMAND");
        mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSETS", NULL, 5000, &reply);
        return;
    }
    assert (msg);
    assert (cfg);

    char* c_command = zmsg_popstr (msg);
    if (! streq (c_command, "GET")) {
        log_error ("%s:\tASSETS: bad command '%s', expected GET", cfg->name, c_command);
        char* uuid = zmsg_popstr (msg);
        if (uuid)
            zmsg_addstr (reply, uuid);
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "BAD_COMMAND");
        mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSETS", NULL, 5000, &reply);
        zstr_free (&c_command);
        if (uuid)
            zstr_free (&uuid);
        return;
    }
    zstr_free (&c_command);
    char* uuid = zmsg_popstr (msg);

    std::set <std::string> filters;
    while (zmsg_size (msg) > 0) {
        char *filter = zmsg_popstr (msg);
        filters.insert (filter);
        zstr_free (&filter);
    }
    std::vector <std::string> assets;
    int rv = 0;

    // if there is no error msg prepared, call SQL
    if (zmsg_size (msg) == 0){
            rv = select_assets_by_filter(filters, assets, cfg->test);
    }

    if (rv == -1)
    {
        zmsg_addstr (reply, uuid);
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "INTERNAL_ERROR");
    }
    else
    if (rv == -2)
    {
        zmsg_addstr (reply, uuid);
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "ASSET_NOT_FOUND");
    }
    else
    {
        zmsg_addstr (reply, uuid);
        zmsg_addstr (reply, "OK");
        for (const auto& dev : assets)
            zmsg_addstr (reply, dev.c_str ());
    }

    // send the reply
    rv = mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSETS", NULL, 5000, &reply);
    if (rv == -1)
        log_error ("%s:\tASSETS: mlm_client_sendto failed", cfg->name);
    zstr_free (&uuid);

}

static zmsg_t *
    s_publish_delete_asset_msg (
        const std::string& asset_name,
        const char *operation,
        std::string & subject)
{
    subject.append ("unknown.unknown");
    subject.append ("@");
    subject.append (asset_name);
    log_debug("notifying ASSETS %s %s ..",operation,subject.c_str());
    zmsg_t *msg = fty_proto_encode_asset (
            NULL,
            asset_name.c_str(),
            operation,
            NULL);
    return msg;
}


static zmsg_t *
    s_publish_create_or_update_asset_msg (
        fty_asset_server_t *cfg,
        const std::string &asset_name,
        const char* operation,
        std::string &subject,
        bool read_only)
{
    assert (cfg);
    zhash_t *aux = zhash_new ();
    zhash_autofree (aux);
    uint32_t asset_id = 0;

    std::function<void(const tntdb::Row&)> cb1 = \
        [aux, &asset_id, asset_name](const tntdb::Row &row)
        {
            int foo_i = 0;
            row ["priority"].get (foo_i);
            zhash_insert (aux, "priority", (void*) std::to_string (foo_i).c_str());

            foo_i = 0;
            row ["id_type"].get (foo_i);
            zhash_insert (aux, "type", (void*) persist::typeid_to_type (foo_i).c_str());

            // additional aux items (requiered by uptime)
            if (streq (persist::typeid_to_type (foo_i).c_str(), "datacenter"))
            {
                if (!DBUptime::get_dc_upses (asset_name.c_str(), aux))
                    log_error ("Cannot read upses for dc with id = %s", asset_name.c_str ());

            }
            foo_i = 0;
            row ["subtype_id"].get (foo_i);
            zhash_insert (aux, "subtype", (void*) persist::subtypeid_to_subtype (foo_i).c_str());

            foo_i = 0;
            row ["id_parent"].get (foo_i);
            zhash_insert (aux, "parent", (void*) std::to_string (foo_i).c_str());

            std::string foo_s;
            row ["status"].get (foo_s);
            zhash_insert (aux, "status", (void*) foo_s.c_str());

            row ["id"].get (asset_id);
        };

    // select basic info
    int rv = select_asset_element_basic (asset_name, cb1, cfg->test);
    if ( rv != 0 ) {
        log_warning ("%s:\tCannot select info about '%s'", cfg->name, asset_name.c_str());
        zhash_destroy (&aux);
        return NULL;
    }

    zhash_t *ext = zhash_new ();
    zhash_autofree (ext);

    std::function<void(const tntdb::Row&)> cb2 = \
        [ext](const tntdb::Row &row)
            {
                std::string keytag;
                row ["keytag"].get (keytag);
                std::string value;
                row ["value"].get (value);
                zhash_insert (ext, keytag.c_str(), (void*) value.c_str());
            };
    // select ext attributes
    rv = select_ext_attributes (asset_id, cb2, cfg->test);
    if ( rv != 0 ) {
        log_warning ("%s:\tCannot select ext attributes for '%s'", cfg->name, asset_name.c_str());
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        return NULL;
    }

    // create uuid ext attribute if missing
    if (! zhash_lookup (ext, "uuid") ) {
        const char *serial = (const char *) zhash_lookup (ext, "serial_no");
        const char *model = (const char *) zhash_lookup (ext, "model");
        const char *mfr = (const char *) zhash_lookup (ext, "manufacturer");
        const char *type = (const char *) zhash_lookup (aux, "type");
        if (! type) type = "";
        fty_uuid_t *uuid = fty_uuid_new ();
        zhash_t *ext_new = zhash_new ();

        if (serial && model && mfr) {
            // we have all information => create uuid
            const char *uuid_new = fty_uuid_calculate (uuid, mfr, model, serial);
            zhash_insert (ext, "uuid", (void *) uuid_new);
            zhash_insert (ext_new, "uuid", (void *) uuid_new);
            process_insert_inventory (asset_name.c_str (), ext_new, true, cfg->test);
        } else {
            if (streq (type, "device")) {
                // it is device, put FFF... and wait for information
                zhash_insert (ext, "uuid", (void *) fty_uuid_calculate (uuid, NULL, NULL, NULL));
            } else {
                // it is not device, we will probably not get more information
                // lets generate random uuid and save it
                const char *uuid_new = fty_uuid_generate (uuid);
                zhash_insert (ext, "uuid", (void *) uuid_new);
                zhash_insert (ext_new, "uuid", (void *) uuid_new);
                process_insert_inventory (asset_name.c_str (), ext_new, true, cfg->test);
            }
        }
        fty_uuid_destroy (&uuid);
        zhash_destroy (&ext_new);
    }

    // create timestamp ext attribute if missing
    if (! zhash_lookup (ext, "create_ts") ) {
        zhash_t *ext_new = zhash_new ();

        std::time_t timestamp = std::time(NULL);
        char mbstr[100];

        std::strftime(mbstr, sizeof (mbstr), "%FT%T%z", std::localtime(&timestamp));

        zhash_insert (ext, "create_ts", (void *) mbstr);
        zhash_insert (ext_new, "create_ts", (void *) mbstr);

        process_insert_inventory (asset_name.c_str (), ext_new, true, cfg->test);

        zhash_destroy (&ext_new);
    }

    std::function<void(const tntdb::Row&)> cb3 = \
        [aux](const tntdb::Row &row)
        {
            for (const auto& name: {"parent_name1", "parent_name2", "parent_name3", "parent_name4",
                                    "parent_name5", "parent_name6", "parent_name7", "parent_name8",
                                    "parent_name9", "parent_name10"}) {
                std::string foo;
                row [name].get (foo);
                std::string hash_name = name;
                //                11 == strlen ("parent_name")
                hash_name.insert (11, 1, '.');
                if (!foo.empty ())
                    zhash_insert (aux, hash_name.c_str (), (void*) foo.c_str ());
            }
        };
    // select "physical topology"
    rv = select_asset_element_super_parent (asset_id, cb3, cfg->test);
    if (rv != 0) {
        zhash_destroy (&aux);
        zhash_destroy (&ext);
        log_error ("%s:\tselect_asset_element_super_parent ('%s') failed.", cfg->name, asset_name.c_str());
        return NULL;
    }
    //TODO: other information like, groups, power chain for now are not included in the message
    const char* type = (const char*) zhash_lookup (aux, "type");
    subject = (type==NULL) ? "unknown" : type;
    subject.append (".");
    const char* subtype =(const char*)zhash_lookup (aux, "subtype");
    subject.append ( (subtype==NULL)?"unknown":subtype );
    subject.append ("@");
    subject.append (asset_name);
    log_debug("notifying ASSETS %s %s ..",operation,subject.c_str());
    zmsg_t *msg = fty_proto_encode_asset (
            aux,
            asset_name.c_str(),
            operation,
            ext);
    zhash_destroy (&ext);
    zhash_destroy (&aux);
    return msg;
}

static void
    s_send_create_or_update_asset (
        fty_asset_server_t *cfg,
        const std::string &asset_name,
        const char* operation,
        bool read_only)
{
    std::string subject;
    zmsg_t *msg;
    if (streq (operation, "FTY_PROTO_ASSET_OP_DELETE"))
    {
        msg = s_publish_delete_asset_msg (asset_name, operation, subject);
    }
    else
    {
        msg = s_publish_create_or_update_asset_msg (cfg, asset_name, operation, subject, read_only);
    }
    if (NULL == msg || 0 != mlm_client_send (cfg->stream_client, subject.c_str(), &msg)) {
        log_info ("%s:\tmlm_client_send not sending message for asset '%s'", cfg->name, asset_name.c_str());
        return;
    }
}

static void
    s_sendto_create_or_update_asset (
        fty_asset_server_t *cfg,
        const std::string &asset_name,
        const char* operation,
        const char *address,
        const char *uuid)
{
    std::string subject;
    auto msg = s_publish_create_or_update_asset_msg(cfg, asset_name, operation, subject, false);
    if (NULL == msg) {
        msg = zmsg_new ();
        log_error ("%s:\tASSET_DETAIL: asset not found", cfg->name);
        zmsg_addstr (msg, "ERROR");
        zmsg_addstr (msg, "ASSET_NOT_FOUND");
    }
    zmsg_pushstr(msg, uuid);
    if (0 != mlm_client_sendto (cfg->mailbox_client, address, subject.c_str(), NULL, 5000, &msg)) {
        log_error ("%s:\tmlm_client_send failed for asset '%s'", cfg->name, asset_name.c_str());
        return;
    }
}

static void
    s_handle_subject_asset_detail (fty_asset_server_t *cfg, zmsg_t **zmessage_p)
{
    if (!cfg || !zmessage_p || !*zmessage_p) return;
    zmsg_t *zmessage = *zmessage_p;
    char* c_command = zmsg_popstr (zmessage);
    if (! streq (c_command, "GET")) {
        char* uuid = zmsg_popstr (zmessage);
        zmsg_t *reply = zmsg_new ();
        log_error ("%s:\tASSET_DETAIL: bad command '%s', expected GET", cfg->name, c_command);
        if (uuid)
            zmsg_addstr (reply, uuid);
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "BAD_COMMAND");
        mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_DETAIL", NULL, 5000, &reply);
        zstr_free (&c_command);
        zmsg_destroy (&reply);
        return;
    }
    zstr_free (&c_command);

    // select an asset and publish it through mailbox
    char* uuid = zmsg_popstr (zmessage);
    char *asset_name = zmsg_popstr (zmessage);
    s_sendto_create_or_update_asset (cfg, asset_name, FTY_PROTO_ASSET_OP_UPDATE, mlm_client_sender (cfg->mailbox_client), uuid);
    zstr_free (&asset_name);
    zstr_free (&uuid);
}

static void
    s_handle_subject_asset_manipulation (fty_asset_server_t *cfg, zmsg_t **zmessage_p)
{
    if (!cfg || !zmessage_p || !*zmessage_p) return;
    zmsg_t *reply = zmsg_new ();

    zmsg_t *zmessage = *zmessage_p;
    char *read_only_str = zmsg_popstr (zmessage);
    bool read_only;
    if (!read_only_str) {
        zmsg_addstr (reply, "ERROR");
        zmsg_addstr (reply, "BAD_COMMAND");
        mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);
        zmsg_destroy (&reply);
        return;
    }
    else {
        if (streq (read_only_str, "READONLY")) {
            read_only = true;
        }
        else if (streq (read_only_str, "READWRITE")) {
            read_only = false;
        }
        else {
            zmsg_addstr (reply, "ERROR");
            zmsg_addstr (reply, "BAD_COMMAND");
            mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);
            zstr_free (&read_only_str);
            zmsg_destroy (&reply);
            return;
        }
    }
    zstr_free (&read_only_str);

    if (!is_fty_proto (zmessage)) {
        log_error ("%s:\tASSET_MANIPULATION: receiver message is not fty_proto", cfg->name);
        return;
    }
    fty_proto_t *fmsg = fty_proto_decode (zmessage_p);
    if (! fmsg) {
        log_error ("%s:\tASSET_MANIPULATION: failed to decode message", cfg->name);
        return;
    }

    const char *operation = fty_proto_operation (fmsg);

    if (streq (operation, FTY_PROTO_ASSET_OP_CREATE) || streq (operation, FTY_PROTO_ASSET_OP_UPDATE) || streq (operation, FTY_PROTO_ASSET_OP_DELETE)) {
        db_reply_t dbreply = create_or_update_asset (fmsg, read_only, cfg->test, &(cfg->limitations));
        if (-1 == dbreply.status && LICENSING_ERR == dbreply.errtype) {
            if (LICENSING_POWER_DEVICES_COUNT_REACHED == dbreply.errsubtype) {
                log_error ("Failed to edit asset due to licensing limitation!");
                fty_proto_print (fmsg);
                zmsg_addstr (reply, "ERROR");
                zmsg_addstr (reply, "Licensing limitation hit - maximum amount of active power devices allowed in license reached.");
                mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);
                fty_proto_destroy (&fmsg);
                return;
            }
            if (LICENSING_GLOBAL_CONFIGURABILITY_DISABLED == dbreply.errsubtype) {
                log_error ("Failed to edit asset due to licensing limitation!");
                fty_proto_print (fmsg);
                zmsg_addstr (reply, "ERROR");
                zmsg_addstr (reply, "Licensing limitation hit - asset manipulation is prohibited.");
                mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);
                fty_proto_destroy (&fmsg);
                return;
            }
        }
        else
        if (! dbreply.status) {
            log_error ("Failed to create asset!");
            fty_proto_print (fmsg);
            zmsg_addstr (reply, "ERROR");
            mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);
            fty_proto_destroy (&fmsg);
            return;
        }
        zmsg_addstr (reply, "OK");
        zmsg_addstr (reply, fty_proto_name (fmsg));
        mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);
        //publish on stream ASSETS
        s_send_create_or_update_asset (cfg, fty_proto_name (fmsg), operation, read_only);
        fty_proto_destroy (&fmsg);
        return;
    }
    // so far no operation implemented
    log_error ("%s:\tASSET_MANIPULATION: asset operation %s is not implemented", cfg->name, operation);
    zmsg_addstr (reply, "ERROR");
    zmsg_addstr (reply, "OPERATION_NOT_IMPLEMENTED");
    mlm_client_sendto (cfg->mailbox_client, mlm_client_sender (cfg->mailbox_client), "ASSET_MANIPULATION", NULL, 5000, &reply);

    fty_proto_destroy (&fmsg);
}

static void
    s_update_topology(
        fty_asset_server_t *cfg,
        fty_proto_t *msg)
{
    assert (msg);
    assert (cfg);

    if ( !streq (fty_proto_operation (msg),FTY_PROTO_ASSET_OP_UPDATE)) {
        log_info ("%s:\tIgnore: '%s' on '%s'", cfg->name, fty_proto_operation(msg), fty_proto_name (msg));
        return;
    }
    // select assets, that were affected by the change
    std::set<std::string> empty;
    std::vector <std::string> asset_names;
    int rv = select_assets_by_container (fty_proto_name (msg), empty, asset_names, cfg->test);
    if ( rv != 0 ) {
        log_warning ("%s:\tCannot select assets in container '%s'", cfg->name, fty_proto_name (msg));
        return;
    }

    // For every asset we need to form new message!
    for ( const auto &asset_name : asset_names ) {
        s_send_create_or_update_asset (cfg, asset_name, FTY_PROTO_ASSET_OP_UPDATE, true);
    }
}

static void
s_repeat_all (fty_asset_server_t *cfg, const std::set<std::string>& assets_to_publish)
{
    assert (cfg);

    std::vector <std::string> asset_names;
    std::function<void(const tntdb::Row&)> cb = \
        [&asset_names, &assets_to_publish](const tntdb::Row &row)
        {
            std::string foo;
            row ["name"].get (foo);
            if (assets_to_publish.size () == 0)
                asset_names.push_back (foo);
            else
            if (assets_to_publish.count (foo) == 1)
                asset_names.push_back (foo);
        };

    // select all assets
    int rv = select_assets (cb, cfg->test);
    if ( rv != 0 ) {
        log_warning ("%s:\tCannot list all assets", cfg->name);
        return;
    }

    // For every asset we need to form new message!
    for ( const auto &asset_name : asset_names ) {
        s_send_create_or_update_asset (cfg, asset_name, FTY_PROTO_ASSET_OP_UPDATE, true);
    }
}

static void
s_repeat_all (fty_asset_server_t *cfg)
{
    return s_repeat_all (cfg, {});
}

void
handle_incoming_limitations (fty_asset_server_t *cfg, fty_proto_t *metric)
{
    // subject matches type.name, so checking those should be sufficient
    assert (fty_proto_id(metric) == FTY_PROTO_METRIC);
    if (streq (fty_proto_name(metric), "rackcontroller-0")) {
        if (streq (fty_proto_type(metric), "power_nodes.max_active")) {
            log_debug("Setting power_nodes/max_active to %s.", fty_proto_value (metric));
            cfg->limitations.max_active_power_devices = atoi (fty_proto_value (metric));
            // based on limitations we may have to limit some features
            // force disable some power nodes
            if (disable_power_nodes_if_limitation_applies(cfg->limitations.max_active_power_devices, cfg->test)) {
                // there was a change, so repeat all is mandatory
                s_repeat_all (cfg);
            }
        } else if (streq (fty_proto_type(metric), "configurability.global")) {
        log_debug("Setting configurability/global to %s.", fty_proto_value (metric));
        cfg->limitations.global_configurability = atoi (fty_proto_value (metric));
        }
    }
}

void
fty_asset_server (zsock_t *pipe, void *args)
{
    assert (pipe);
    assert (args);

    fty_asset_server_t *cfg = fty_asset_server_new ();
    assert (cfg);
    cfg->name = strdup ((char*) args);
    assert (cfg->name);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe(cfg->mailbox_client), mlm_client_msgpipe(cfg->stream_client), NULL);
    assert (poller);

    // Signal need to be send as it is required by "actor_new"
    zsock_signal (pipe, 0);
    log_info ("%s:\tStarted", cfg->name);

    while (!zsys_interrupted) {

        void *which = zpoller_wait (poller, -1);
        if ( !which ) {
            // cannot expire as waiting until infinity
            // so it is interrupted
            break;
        }

        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);
            log_debug ("%s:\tActor command=%s", cfg->name, cmd);

            if (streq (cmd, "$TERM")) {
                log_info ("%s:\tGot $TERM", cfg->name);
                zstr_free (&cmd);
                zmsg_destroy (&msg);
                goto exit;
            }
            else
            if (streq (cmd, "CONNECTSTREAM")) {
                char* endpoint = zmsg_popstr (msg);
                char *stream_name = zsys_sprintf ("%s-stream", cfg->name);
                int rv = mlm_client_connect (cfg->stream_client, endpoint, 1000, stream_name);
                if (rv == -1) {
                    log_error ("%s:\tCan't connect to malamute endpoint '%s'", stream_name, endpoint);
                }
                zstr_free (&endpoint);
                zstr_free (&stream_name);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "PRODUCER")) {
                char* stream = zmsg_popstr (msg);
                cfg->test = streq (stream, "ASSETS-TEST");
                int rv = mlm_client_set_producer (cfg->stream_client, stream);
                if (rv == -1) {
                    log_error ("%s:\tCan't set producer on stream '%s'", cfg->name, stream);
                }
                zstr_free (&stream);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "CONSUMER")) {
                char* stream = zmsg_popstr (msg);
                char* pattern = zmsg_popstr (msg);
                int rv = mlm_client_set_consumer (cfg->stream_client, stream, pattern);
                if (rv == -1) {
                    log_error ("%s:\tCan't set consumer on stream '%s', '%s'", cfg->name, stream, pattern);
                }
                zstr_free (&pattern);
                zstr_free (&stream);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "CONNECTMAILBOX")) {
                char* endpoint = zmsg_popstr (msg);
                int rv = mlm_client_connect (cfg->mailbox_client, endpoint, 1000, cfg->name);
                if (rv == -1) {
                    log_error ("%s:\tCan't connect to malamute endpoint '%s'", cfg->name, endpoint);
                }
                zstr_free (&endpoint);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "REPEAT_ALL")) {
                s_repeat_all (cfg);
                log_debug ("%s:\tREPEAT_ALL end", cfg->name);
            }
            else
            {
                log_info ("%s:\tUnhandled command %s", cfg->name, cmd);
            }
            zstr_free (&cmd);
            zmsg_destroy (&msg);
            continue;
        }

        // This agent is a reactive agent, it reacts only on messages
        // and doesn't do anything if there are no messages
        else if (which == mlm_client_msgpipe (cfg->mailbox_client)) {
            zmsg_t *zmessage = mlm_client_recv (cfg->mailbox_client);
            if ( zmessage == NULL ) {
                continue;
            }
            std::string subject = mlm_client_subject (cfg->mailbox_client);
            if (subject == "TOPOLOGY")
                s_handle_subject_topology (cfg, zmessage);
            else
            if (subject == "ASSETS_IN_CONTAINER")
                s_handle_subject_assets_in_container (cfg, zmessage);
            else
            if (subject == "ASSETS")
                s_handle_subject_assets (cfg, zmessage);
            else
            if (subject == "ENAME_FROM_INAME") {
                s_handle_subject_ename_from_iname(cfg, zmessage);
            }
            else
            if (subject == "REPUBLISH") {

                zmsg_print (zmessage);
                log_trace ("REPUBLISH received from '%s'",mlm_client_sender (cfg->mailbox_client));
                char *asset = zmsg_popstr (zmessage);
                if (!asset)
                    s_repeat_all (cfg);
                else if (streq (asset, "$all")) {
                    zstr_free (&asset);
                    s_repeat_all (cfg);
                }
                else {
                    std::set <std::string> assets_to_publish;
                    while (asset) {
                        assets_to_publish.insert (asset);
                        zstr_free (&asset);
                        asset = zmsg_popstr (zmessage);
                    }
                    s_repeat_all (cfg, assets_to_publish);
                }
            }
            else
            if (subject == "ASSET_MANIPULATION") {
                s_handle_subject_asset_manipulation (cfg, &zmessage);
            }
            else
            if (subject == "ASSET_DETAIL") {
                s_handle_subject_asset_detail (cfg, &zmessage);
            }
            else
                log_info ("%s:\tUnexpected subject '%s'", cfg->name, subject.c_str ());
            zmsg_destroy (&zmessage);
        }
        else if (which == mlm_client_msgpipe (cfg->stream_client)) {
            zmsg_t *zmessage = mlm_client_recv (cfg->stream_client);
            if ( zmessage == NULL ) {
                continue;
            }
            if ( is_fty_proto (zmessage) ) {
                fty_proto_t *bmsg = fty_proto_decode (&zmessage);
                if ( fty_proto_id (bmsg) == FTY_PROTO_ASSET ) {
                    s_update_topology (cfg, bmsg);
                } else if ( fty_proto_id (bmsg) == FTY_PROTO_METRIC ) {
                    handle_incoming_limitations (cfg, bmsg);
                }
                fty_proto_destroy (&bmsg);
            }
            else {
                // DO NOTHING for now
                zmsg_destroy (&zmessage);
            }
        }
        else {
            // DO NOTHING for now
        }
    }
exit:
    log_info ("%s:\tended", cfg->name);
    //TODO:  save info to persistence before I die
    zpoller_destroy (&poller);
    fty_asset_server_destroy (&cfg);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_asset_server_test (bool verbose)
{
    printf (" * fty_asset_server: ");
    //  @selftest
    // Test #1:  Simple create/destroy test
    {
        log_debug ("fty-asset-server-test:Test #1");
        fty_asset_server_t *self = fty_asset_server_new ();
        assert (self);
        fty_asset_server_destroy (&self);
        log_info ("fty-asset-server-test:Test #1: OK");
    }

    static const char* endpoint = "inproc://fty_asset_server-test";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    assert ( server != NULL );
    zstr_sendx (server, "BIND", endpoint, NULL);

    mlm_client_t *ui = mlm_client_new ();
    mlm_client_connect (ui, endpoint, 5000, "fty-asset-ui");
    mlm_client_set_producer (ui, "ASSETS-TEST");
    mlm_client_set_consumer (ui, "ASSETS-TEST", ".*");

    const char *asset_server_test_name = "asset_agent_test";
    zactor_t *asset_server = zactor_new (fty_asset_server, (void*) asset_server_test_name);

    zstr_sendx (asset_server, "CONNECTSTREAM", endpoint, NULL);
    zsock_wait (asset_server);
    zstr_sendx (asset_server, "PRODUCER", "ASSETS-TEST", NULL);
    zsock_wait (asset_server);
    zstr_sendx (asset_server, "CONSUMER", "ASSETS-TEST", ".*", NULL);
    zsock_wait (asset_server);
    zstr_sendx (asset_server, "CONSUMER", "LICENSING-ANNOUNCEMENTS-TEST", ".*", NULL);
    zsock_wait (asset_server);
    zstr_sendx (asset_server, "CONNECTMAILBOX", endpoint, NULL);
    zsock_wait (asset_server);
    static const char *asset_name = TEST_INAME;
    // Test #2: subject ASSET_MANIPULATION, message fty_proto_t *asset
    {
        log_debug ("fty-asset-server-test:Test #2");
        const char* subject = "ASSET_MANIPULATION";
        zhash_t *aux = zhash_new();
        //zhash_autofree (aux);
        zhash_insert (aux, "type", (void *)"datacenter");
        zhash_insert (aux, "subtype", (void *)"N_A");
        zmsg_t *msg = fty_proto_encode_asset (
                aux,
                asset_name,
                FTY_PROTO_ASSET_OP_CREATE,
                NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        zhash_destroy (&aux);
        assert (rv == 0);
        zmsg_t *reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            char *str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
            str = zmsg_popstr (reply);
            assert (streq (str, asset_name));
            zstr_free (&str);
            zmsg_destroy (&reply);
        }
        else {
            assert (is_fty_proto (reply));
            fty_proto_t *fmsg = fty_proto_decode (&reply);
            std::string expected_subject="unknown.unknown@";
            expected_subject.append(asset_name);
            assert(streq(mlm_client_subject (ui),expected_subject.c_str()));
            assert (streq (fty_proto_operation (fmsg), FTY_PROTO_ASSET_OP_CREATE));
            fty_proto_destroy (&fmsg);
            zmsg_destroy (&reply) ;
        }

        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            char *str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
            str = zmsg_popstr (reply);
            assert (streq (str, asset_name));
            zstr_free (&str);
            zmsg_destroy (&reply);
        }
        else {
            assert (is_fty_proto (reply));
            fty_proto_t *fmsg = fty_proto_decode (&reply);
            std::string expected_subject="unknown.unknown@";
            expected_subject.append(asset_name);
            assert(streq(mlm_client_subject (ui),expected_subject.c_str()));
            assert (streq (fty_proto_operation (fmsg), FTY_PROTO_ASSET_OP_CREATE));
            fty_proto_destroy (&fmsg);
            zmsg_destroy (&reply) ;
        }
        log_info ("fty-asset-server-test:Test #2: OK");
    }

    // Test #3: message fty_proto_t *asset
    {
        log_debug ("fty-asset-server-test:Test #3");
        zmsg_t *msg = fty_proto_encode_asset (
            NULL,
            asset_name,
            FTY_PROTO_ASSET_OP_UPDATE,
            NULL);
        int rv = mlm_client_send (ui, "update-test", &msg);
        assert (rv == 0);
        zclock_sleep (200);
        log_info ("fty-asset-server-test:Test #3: OK");
    }
    // Test #4: subject TOPOLOGY, message TOPOLOGY_POWER
    {
        log_debug ("fty-asset-server-test:Test #4");
        const char* subject = "TOPOLOGY";
        const char *command = "TOPOLOGY_POWER";
        zmsg_t *msg = zmsg_new();
        zmsg_addstr (msg, command);
        zmsg_addstr (msg, asset_name);
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        assert (rv == 0);
        zmsg_t *reply = mlm_client_recv (ui);
        assert (streq (mlm_client_subject (ui), subject));
        assert (zmsg_size (reply) == 3);
        char *str = zmsg_popstr (reply);
        assert (streq (str, command));
        zstr_free (&str);
        str = zmsg_popstr (reply);
        assert (streq (str, asset_name));
        zstr_free (&str);
        str = zmsg_popstr (reply);
        assert (streq (str, "OK"));
        zstr_free (&str);
        zmsg_destroy (&reply) ;
        log_info ("fty-asset-server-test:Test #4: OK");
    }
    // Test #5: subject ASSETS_IN_CONTAINER, message GET
    {
        log_debug ("fty-asset-server-test:Test #5");
        const char* subject = "ASSETS_IN_CONTAINER";
        const char *command = "GET";
        zmsg_t *msg = zmsg_new();
        zmsg_addstr (msg, command);
        zmsg_addstr (msg, asset_name);
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        assert (rv == 0);
        zmsg_t *reply = mlm_client_recv (ui);
        assert (streq (mlm_client_subject (ui), subject));
        assert (zmsg_size (reply) == 1);
        char *str = zmsg_popstr (reply);
        assert (streq (str, "OK"));
        zstr_free (&str);
        zmsg_destroy (&reply) ;
        log_info ("fty-asset-server-test:Test #5: OK");
    }
    // Test #6: subject ASSETS, message GET
    {
        log_debug ("fty-asset-server-test:Test #6");
        const char* subject = "ASSETS";
        const char *command = "GET";
        zmsg_t *msg = zmsg_new();
        zmsg_addstr (msg, command);
        zmsg_addstr (msg, "UUID");
        zmsg_addstr (msg, asset_name);
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        assert (rv == 0);
        zmsg_t *reply = mlm_client_recv (ui);
        assert (streq (mlm_client_subject (ui), subject));
        assert (zmsg_size (reply) == 2);
        char *uuid = zmsg_popstr (reply);
        assert (streq (uuid, "UUID"));
        char *str = zmsg_popstr (reply);
        assert (streq (str, "OK"));
        zstr_free (&str);
        zstr_free (&uuid);
        zmsg_destroy (&reply) ;
        log_info ("fty-asset-server-test:Test #6: OK");
    }
    // Test #7: message REPEAT_ALL
    {
        log_debug ("fty-asset-server-test:Test #7");
        const char *command = "REPEAT_ALL";
        int rv = zstr_sendx (asset_server, command, NULL);
        assert (rv == 0);
        zclock_sleep (200);
        log_info ("fty-asset-server-test:Test #7: OK");
    }
    // Test #8: subject REPUBLISH, message $all
    {
        log_debug ("fty-asset-server-test:Test #8");
        const char *subject = "REPUBLISH";
        zmsg_t *msg = zmsg_new();
        zmsg_addstr (msg, "$all");
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        assert (rv == 0);
        zclock_sleep (200);
        log_info ("fty-asset-server-test:Test #8: OK");
    }
    // Test #9: subject ASSET_DETAIL, message GET/<iname>
    {
        log_debug ("fty-asset-server-test:Test #9");
        const char* subject = "ASSET_DETAIL";
        const char *command = "GET";
        const char *uuid = "UUID-0000-TEST";
        zmsg_t *msg = zmsg_new();
        zmsg_addstr (msg, command);
        zmsg_addstr (msg, uuid);
        zmsg_addstr (msg, asset_name);
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        assert (rv == 0);
        zmsg_t *reply = mlm_client_recv (ui);
        char *rcv_uuid = zmsg_popstr (reply);
        assert (0 == strcmp (rcv_uuid, uuid));
        assert (fty_proto_is (reply));
        fty_proto_t *freply = fty_proto_decode (&reply);
        const char *str = fty_proto_name (freply);
        assert (streq (str, asset_name));
        str = fty_proto_operation (freply);
        assert (streq (str, FTY_PROTO_ASSET_OP_UPDATE));
        fty_proto_destroy (&freply);
        zstr_free (&rcv_uuid);
        log_info ("fty-asset-server-test:Test #9: OK");
    }

    // Test #10: subject ENAME_FROM_INAME, message <iname>
    {
        log_debug ("fty-asset-server-test:Test #10");
        const char* subject = "ENAME_FROM_INAME";
        const char *asset_ename = TEST_ENAME;
        zmsg_t *msg = zmsg_new();
        zmsg_addstr (msg, asset_name);
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        assert (rv == 0);
        zmsg_t *reply = mlm_client_recv (ui);
        assert (zmsg_size (reply) == 2);
        char *str = zmsg_popstr (reply);
        assert (streq (str, "OK"));
        zstr_free (&str);
        str = zmsg_popstr (reply);
        assert (streq (str, asset_ename));
        zstr_free (&str);
        zmsg_destroy (&reply);
        log_info ("fty-asset-server-test:Test #10: OK");
    }
    zactor_t *autoupdate_server = zactor_new (fty_asset_autoupdate_server, (void*) "asset-autoupdate-test");
    zstr_sendx (autoupdate_server, "CONNECT", endpoint, NULL);
    zsock_wait (autoupdate_server);
    zstr_sendx (autoupdate_server, "PRODUCER", "ASSETS-TEST", NULL);
    zsock_wait (autoupdate_server);
    zstr_sendx (autoupdate_server, "ASSET_AGENT_NAME", asset_server_test_name, NULL);

    // Test #11: message WAKEUP
    {
        log_debug ("fty-asset-server-test:Test #11");
        const char *command = "WAKEUP";
        int rv = zstr_sendx (autoupdate_server, command, NULL);
        assert (rv == 0);
        zclock_sleep (200);
        log_info ("fty-asset-server-test:Test #11: OK");
    }

    // Test #12: test licensing limitations
    /*{
        log_debug ("fty-asset-server-test:Test #12");
        // try to create asset when configurability is enabled
        const char* subject = "ASSET_MANIPULATION";
        zmsg_t *msg = fty_proto_encode_asset (
                NULL,
                asset_name,
                FTY_PROTO_ASSET_OP_CREATE,
                NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        int rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        zclock_sleep (200);
        assert (rv == 0);
        char *str = NULL;
        zmsg_t *reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply); // throw away stream message


        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply) ; // throw away stream message

        // disable configurability
        mlm_client_set_producer (ui, "LICENSING-ANNOUNCEMENTS-TEST");
        zmsg_t *smsg = fty_proto_encode_metric ( NULL, time(NULL), 24*60*60, "configurability.global", "rackcontroller-0", "0", "");
        mlm_client_send (ui, "configurability.global@rackcontroller-0", &smsg);
        zclock_sleep (200);
        // try to create asset when configurability is disabled
        msg = fty_proto_encode_asset (
                NULL,
                asset_name,
                FTY_PROTO_ASSET_OP_CREATE,
                NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        zclock_sleep (200);
        assert (rv == 0);
        reply = mlm_client_recv (ui);
        assert (streq (mlm_client_subject (ui), subject));
        assert (zmsg_size (reply) == 2);
        str = zmsg_popstr (reply);
        assert (streq (str, "ERROR"));
        zstr_free (&str);
        str = zmsg_popstr (reply);
        assert (streq (str, "Licensing limitation hit - asset manipulation is prohibited."));
        zstr_free (&str);
        zmsg_destroy (&reply);
        // enable configurability again, but set limit to power devices
        smsg = fty_proto_encode_metric ( NULL, time(NULL), 24*60*60, "configurability.global", "rackcontroller-0", "1", "");
        mlm_client_send (ui, "configurability.global@rackcontroller-0", &smsg);
        smsg = fty_proto_encode_metric ( NULL, time(NULL), 24*60*60, "power_nodes.max_active", "rackcontroller-0", "3", "");
        mlm_client_send (ui, "power_nodes.max_active@rackcontroller-0", &smsg);
        zclock_sleep (300);
        // send power devices
        zhash_t *aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "type", (void *) "device");
        zhash_insert (aux, "subtype", (void *) "epdu");
        zhash_insert (aux, "status", (void *) "active");
        msg = fty_proto_encode_asset (aux,
                        "test1",
                        FTY_PROTO_ASSET_OP_UPDATE,
                        NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        if (aux)
            zhash_destroy(&aux);
        zclock_sleep (200);
        assert (rv == 0);
        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply); // throw away stream message

        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply) ; // throw away stream message

        aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "type", (void *) "device");
        zhash_insert (aux, "subtype", (void *) "epdu");
        zhash_insert (aux, "status", (void *) "active");
        msg = fty_proto_encode_asset (aux,
                        "test2",
                        FTY_PROTO_ASSET_OP_UPDATE,
                        NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        if (aux)
            zhash_destroy(&aux);
        zclock_sleep (1000);
        assert (rv == 0);
        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply); // throw away stream message

        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply) ; // throw away stream message

        aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "type", (void *) "device");
        zhash_insert (aux, "subtype", (void *) "epdu");
        zhash_insert (aux, "status", (void *) "active");
        msg = fty_proto_encode_asset (aux,
                        "test3",
                        FTY_PROTO_ASSET_OP_UPDATE,
                        NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        if (aux)
            zhash_destroy(&aux);
        zclock_sleep (200);
        assert (rv == 0);

        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply); // throw away stream message

        reply = mlm_client_recv (ui);
        if (!fty_proto_is (reply)) {
            assert (streq (mlm_client_subject (ui), subject));
            assert (zmsg_size (reply) == 2);
            str = zmsg_popstr (reply);
            assert (streq (str, "OK"));
            zstr_free (&str);
        }
        zmsg_destroy (&reply) ; // throw away stream message

        aux = zhash_new ();
        zhash_autofree (aux);
        zhash_insert (aux, "type", (void *) "device");
        zhash_insert (aux, "subtype", (void *) "epdu");
        zhash_insert (aux, "status", (void *) "active");
        msg = fty_proto_encode_asset (aux,
                        "test4",
                        FTY_PROTO_ASSET_OP_UPDATE,
                        NULL);
        zmsg_pushstrf (msg, "%s", "READWRITE");
        rv = mlm_client_sendto (ui, asset_server_test_name, subject, NULL, 5000, &msg);
        if (aux)
            zhash_destroy(&aux);
        zclock_sleep (200);
        assert (rv == 0);
        reply = mlm_client_recv (ui);
        assert (streq (mlm_client_subject (ui), subject));
        assert (zmsg_size (reply) == 2);
        str = zmsg_popstr (reply);
        assert (streq (str, "ERROR"));
        zstr_free (&str);
        str = zmsg_popstr (reply);
        assert (streq (str, "Licensing limitation hit - maximum amount of active power devices allowed in license reached."));
        zstr_free (&str);
        zmsg_destroy (&reply);*/
        /* // to be replaced by integration tests
        // request republish to check what assets are published and if all are present as expected
        zpoller_t *poller = zpoller_new (mlm_client_msgpipe(ui), NULL);
        assert (poller);
        subject = "REPEAT_ALL";
        rv = zstr_sendx (asset_server, subject, NULL);
        zclock_sleep (200);
        assert (rv == 0);
        zclock_sleep (200);
        int asset_test1 = 0;
        int asset_test2 = 0;
        int asset_test3 = 0;
        int asset_test4 = 0;
        int asset_test1_enabled = 0;
        int asset_test2_enabled = 0;
        int asset_test3_enabled = 0;
        while (!zsys_interrupted) {
            void *which = zpoller_wait (poller, 200);
            if ( !which ) {
                // nothing more to parse
                break;
            }
            // otherwise message was received
            reply = mlm_client_recv (ui);
            assert (is_fty_proto (reply));
            fty_proto_t *fmsg = fty_proto_decode (&reply);
            if (streq ("test1", fty_proto_name (fmsg))) {
                asset_test1 = 1;
                if (streq ("actve", fty_proto_aux_string (fmsg, "status", "nonactive")))
                    asset_test1_enabled = 1;
            }
            else if (streq ("test2", fty_proto_name (fmsg))) {
                asset_test2 = 1;
                if (streq ("actve", fty_proto_aux_string (fmsg, "status", "nonactive")))
                    asset_test2_enabled = 1;
            }
            else if (streq ("test3", fty_proto_name (fmsg))) {
                asset_test3 = 1;
                if (streq ("actve", fty_proto_aux_string (fmsg, "status", "nonactive")))
                    asset_test3_enabled = 1;
            }
            else if (streq ("test4", fty_proto_name (fmsg))) {
                asset_test4 = 1;
            }
            fty_proto_destroy (&fmsg);
            zmsg_destroy (&reply);
        }
        assert(asset_test1 && asset_test2 && asset_test3 && !asset_test4); // all but test4 must be present
        assert(asset_test1_enabled && asset_test2_enabled && asset_test3_enabled);
        // set power devices limitation to 2 and see if one gets disabled
        mlm_client_sendx (ui, "3", "MAX_ACTIVE", "POWER_NODES", NULL);
        zclock_sleep (200);
        // republish and check assets
        subject = "REPEAT_ALL";
        rv = zstr_sendx (asset_server, subject, NULL);
        zclock_sleep (200);
        assert (rv == 0);
        asset_test1 = 0;
        asset_test2 = 0;
        asset_test3 = 0;
        asset_test4 = 0;
        int active_power_assets = 0;
        while (!zsys_interrupted) {
            void *which = zpoller_wait (poller, 200);
            if ( !which ) {
                // nothing more to parse
                break;
            }
            // otherwise message was received
            reply = mlm_client_recv (ui);
            assert (is_fty_proto (reply));
            fty_proto_t *fmsg = fty_proto_decode (&reply);
            if (streq ("test1", fty_proto_name (fmsg))) {
                asset_test1 = 1;
                if (streq ("actve", fty_proto_aux_string (fmsg, "status", "nonactive")))
                    ++active_power_assets;
            }
            else if (streq ("test2", fty_proto_name (fmsg))) {
                asset_test2 = 1;
                if (streq ("actve", fty_proto_aux_string (fmsg, "status", "nonactive")))
                    ++active_power_assets;
            }
            else if (streq ("test3", fty_proto_name (fmsg))) {
                asset_test3 = 1;
                if (streq ("actve", fty_proto_aux_string (fmsg, "status", "nonactive")))
                    ++active_power_assets;
            }
            fty_proto_destroy (&fmsg);
            zmsg_destroy (&reply);
        }
        assert(asset_test1 && asset_test2 && asset_test3); // all three must be present
        assert(2 == active_power_assets);
        */
    /*}*/

    zactor_destroy (&autoupdate_server);
    zactor_destroy (&asset_server);
    mlm_client_destroy (&ui);
    zactor_destroy (&server);

    //  @end
    printf ("OK\n");
}
