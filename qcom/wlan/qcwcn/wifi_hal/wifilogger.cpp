/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *  * Neither the name of The Linux Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sync.h"

#include "wifi_hal.h"
#include "common.h"
#include "cpp_bindings.h"
#include <errno.h>
#include <utils/Log.h>
#include "wifiloggercmd.h"
#include "rb_wrapper.h"

#define LOGGER_MEMDUMP_FILENAME "/proc/debug/fwdump"
#define LOGGER_MEMDUMP_CHUNKSIZE (4 * 1024)

char power_events_ring_name[] = "power_events_rb";
char connectivity_events_ring_name[] = "connectivity_events_rb";
char pkt_stats_ring_name[] = "pkt_stats_rb";
char driver_prints_ring_name[] = "driver_prints_rb";
char firmware_prints_ring_name[] = "firmware_prints_rb";

static int get_ring_id(hal_info *info, char *ring_name)
{
    int rb_id;

    for (rb_id = 0; rb_id < NUM_RING_BUFS; rb_id++) {
        if (is_rb_name_match(&info->rb_infos[rb_id], ring_name)) {
           return rb_id;
        }
    }
    return -1;
}

//Implementation of the functions exposed in wifi_logger.h

/* Function to intiate logging */
wifi_error wifi_start_logging(wifi_interface_handle iface,
                              u32 verbose_level, u32 flags,
                              u32 max_interval_sec, u32 min_data_size,
                              char *buffer_name)
{
    int requestId, ret = 0;
    WifiLoggerCommand *wifiLoggerCommand = NULL;
    struct nlattr *nlData;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);
    int ring_id = 0;

    /*
     * No request id from caller, so generate one and pass it on to the driver.
     * Generate one randomly.
     */
    requestId = rand();

    if (buffer_name == NULL) {
        ALOGE("%s: Invalid Ring Name. \n", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }

    ring_id = get_ring_id(info, buffer_name);
    if (ring_id < 0) {
        ALOGE("%s: Invalid Ring Buffer Name ", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }

    if ((ring_id == POWER_EVENTS_RB_ID) ||
        (ring_id == PKT_STATS_RB_ID)) {
        wifiLoggerCommand = new WifiLoggerCommand(
                                wifiHandle,
                                requestId,
                                OUI_QCA,
                                QCA_NL80211_VENDOR_SUBCMD_WIFI_LOGGER_START);

        if (wifiLoggerCommand == NULL) {
           ALOGE("%s: Error WifiLoggerCommand NULL", __FUNCTION__);
           return WIFI_ERROR_UNKNOWN;
        }
        /* Create the NL message. */
        ret = wifiLoggerCommand->create();

        if (ret < 0)
            goto cleanup;

        /* Set the interface Id of the message. */
        ret = wifiLoggerCommand->set_iface_id(ifaceInfo->name);

        if (ret < 0)
            goto cleanup;

        /* Add the vendor specific attributes for the NL command. */
        nlData = wifiLoggerCommand->attr_start(NL80211_ATTR_VENDOR_DATA);

        if (!nlData)
            goto cleanup;

        if (wifiLoggerCommand->put_u32(
                    QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_RING_ID, ring_id))
        {
            goto cleanup;
        }
        if (wifiLoggerCommand->put_u32(
                    QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_VERBOSE_LEVEL,
                    verbose_level))
        {
            goto cleanup;
        }
        if (wifiLoggerCommand->put_u32(
                    QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_FLAGS,
                    flags))
        {
            goto cleanup;
        }

        wifiLoggerCommand->attr_end(nlData);

        /* Send the msg and wait for a response. */
        ret = wifiLoggerCommand->requestResponse();
        if (ret) {
            ALOGE("%s: Error %d happened. ", __FUNCTION__, ret);
        }

    }
    ALOGI("%s: Logging Started for %s.", __FUNCTION__, buffer_name);
    rb_start_logging(&info->rb_infos[ring_id], verbose_level,
                    flags, max_interval_sec, min_data_size);
cleanup:
    if (wifiLoggerCommand)
        delete wifiLoggerCommand;
    return (wifi_error)ret;

}

/*  Function to get each ring related info */
wifi_error wifi_get_ring_buffers_status(wifi_interface_handle iface,
                                        u32 *num_buffers,
                                        wifi_ring_buffer_status *status)
{
    int ret = 0;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);
    wifi_ring_buffer_status *rbs;
    struct rb_info *rb_info;
    int rb_id;

    if ((*num_buffers) < NUM_RING_BUFS) {
        ALOGE("%s: Input num_buffers:%d cannot be accommodated, "
              "Total ring buffer num:%d", __FUNCTION__, num_buffers,
              NUM_RING_BUFS);
        *num_buffers = 0;
        return WIFI_ERROR_OUT_OF_MEMORY;
    }
    for (rb_id = 0; rb_id < NUM_RING_BUFS; rb_id++) {
        rb_info = &info->rb_infos[rb_id];
        rbs = status + rb_id;

        get_rb_status(rb_info, rbs);
    }
    *num_buffers = NUM_RING_BUFS;
    return (wifi_error)ret;
}

void push_out_all_ring_buffers(hal_info *info)
{
    int rb_id;

    for (rb_id = 0; rb_id < NUM_RING_BUFS; rb_id++) {
        push_out_rb_data(&info->rb_infos[rb_id]);
    }
}

void send_alert(hal_info *info, int reason_code)
{
    //TODO check locking
    if (info->on_alert) {
        info->on_alert(0, NULL, 0, reason_code);
    }
}

void WifiLoggerCommand::setFeatureSet(u32 *support) {
    mSupportedSet = support;
}

/*  Function to get the supported feature set for logging.*/
wifi_error wifi_get_logger_supported_feature_set(wifi_interface_handle iface,
                                                 u32 *support)
{

    int requestId, ret = 0;
    WifiLoggerCommand *wifiLoggerCommand;
    struct nlattr *nlData;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    /* No request id from caller, so generate one and pass it on to the driver.
     * Generate one randomly.
     */
    requestId = rand();

    wifiLoggerCommand = new WifiLoggerCommand(
                            wifiHandle,
                            requestId,
                            OUI_QCA,
                            QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE_SET);

    if (wifiLoggerCommand == NULL) {
        ALOGE("%s: Error WifiLoggerCommand NULL", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }
    /* Create the NL message. */
    ret = wifiLoggerCommand->create();

    if (ret < 0)
        goto cleanup;

    /* Set the interface Id of the message. */
    ret = wifiLoggerCommand->set_iface_id(ifaceInfo->name);

    if (ret < 0)
        goto cleanup;

    /* Add the vendor specific attributes for the NL command. */
    nlData = wifiLoggerCommand->attr_start(NL80211_ATTR_VENDOR_DATA);

    if (!nlData)
        goto cleanup;

    if (wifiLoggerCommand->put_u32(
                QCA_WLAN_VENDOR_ATTR_FEATURE_SET, requestId))
    {
        goto cleanup;
    }
    wifiLoggerCommand->attr_end(nlData);

    wifiLoggerCommand->setFeatureSet(support);

    /* Send the msg and wait for a response. */
    ret = wifiLoggerCommand->requestResponse();
    if (ret) {
        ALOGE("%s: Error %d happened. ", __FUNCTION__, ret);
    }

cleanup:
    delete wifiLoggerCommand;
    return (wifi_error)ret;
}

/*  Function to get the data in each ring for the given ring ID.*/
wifi_error wifi_get_ring_data(wifi_interface_handle iface,
                              char *ring_name)
{

    int requestId, ret = 0;
    WifiLoggerCommand *wifiLoggerCommand;
    struct nlattr *nlData;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);
    int ring_id = 0;

    ring_id = get_ring_id(info, ring_name);
    if (ring_id < 0) {
        ALOGE("%s: Invalid Ring Buffer Name ", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }

    requestId = rand();

    wifiLoggerCommand = new WifiLoggerCommand(
                                wifiHandle,
                                requestId,
                                OUI_QCA,
                                QCA_NL80211_VENDOR_SUBCMD_GET_RING_DATA);
    if (wifiLoggerCommand == NULL) {
        ALOGE("%s: Error WifiLoggerCommand NULL", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }
    /* Create the NL message. */
    ret = wifiLoggerCommand->create();

    if (ret < 0)
        goto cleanup;

    /* Set the interface Id of the message. */
    ret = wifiLoggerCommand->set_iface_id(ifaceInfo->name);

    if (ret < 0)
        goto cleanup;

    /* Add the vendor specific attributes for the NL command. */
    nlData = wifiLoggerCommand->attr_start(NL80211_ATTR_VENDOR_DATA);

    if (!nlData)
        goto cleanup;

    if (wifiLoggerCommand->put_u32(
                QCA_WLAN_VENDOR_ATTR_WIFI_LOGGER_RING_ID, ring_id))
    {
        goto cleanup;
    }
    wifiLoggerCommand->attr_end(nlData);

    //TBD  Is there requestResponse here
    /* Send the msg and wait for a response. */
    ret = wifiLoggerCommand->requestResponse();
    if (ret) {
        ALOGE("%s: Error %d happened. ", __FUNCTION__, ret);
    }

cleanup:
    delete wifiLoggerCommand;
    return (wifi_error)ret;
}

void WifiLoggerCommand::setVersionInfo(char *buffer, int buffer_size) {
    mVersion = buffer;
    mVersionLen = buffer_size;
}

/*  Function to send enable request to the wifi driver.*/
wifi_error wifi_get_firmware_version(wifi_interface_handle iface,
                                     char *buffer, int buffer_size)
{
    int requestId, ret = 0;
    WifiLoggerCommand *wifiLoggerCommand;
    struct nlattr *nlData;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    /* No request id from caller, so generate one and pass it on to the driver.
     * Generate one randomly.
     */
    requestId = rand();

    wifiLoggerCommand = new WifiLoggerCommand(
                                wifiHandle,
                                requestId,
                                OUI_QCA,
                                QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO);
    if (wifiLoggerCommand == NULL) {
        ALOGE("%s: Error WifiLoggerCommand NULL", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }
    /* Create the NL message. */
    ret = wifiLoggerCommand->create();

    if (ret < 0)
        goto cleanup;

    /* Set the interface Id of the message. */
    ret = wifiLoggerCommand->set_iface_id(ifaceInfo->name);

    if (ret < 0)
        goto cleanup;

    /* Add the vendor specific attributes for the NL command. */
    nlData = wifiLoggerCommand->attr_start(NL80211_ATTR_VENDOR_DATA);

    if (!nlData)
        goto cleanup;

    if (wifiLoggerCommand->put_u32(
                QCA_WLAN_VENDOR_ATTR_WIFI_INFO_FIRMWARE_VERSION, requestId))
    {
        goto cleanup;
    }
    wifiLoggerCommand->attr_end(nlData);

    wifiLoggerCommand->setVersionInfo(buffer, buffer_size);

    /* Send the msg and wait for a response. */
    ret = wifiLoggerCommand->requestResponse();
    if (ret) {
        ALOGE("%s: Error %d happened. ", __FUNCTION__, ret);
    }
cleanup:
    delete wifiLoggerCommand;
    return (wifi_error)ret;

}

/*  Function to get wlan driver version.*/
wifi_error wifi_get_driver_version(wifi_interface_handle iface,
                                   char *buffer, int buffer_size)
{

    int requestId, ret = 0;
    WifiLoggerCommand *wifiLoggerCommand;
    struct nlattr *nlData;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    /* No request id from caller, so generate one and pass it on to the driver.
     * Generate one randomly.
     */
    requestId = rand();

    wifiLoggerCommand = new WifiLoggerCommand(
                            wifiHandle,
                            requestId,
                            OUI_QCA,
                            QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO);
    if (wifiLoggerCommand == NULL) {
        ALOGE("%s: Error WifiLoggerCommand NULL", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }
    /* Create the NL message. */
    ret = wifiLoggerCommand->create();

    if (ret < 0)
        goto cleanup;

    /* Set the interface Id of the message. */
    ret = wifiLoggerCommand->set_iface_id(ifaceInfo->name);

    if (ret < 0)
        goto cleanup;

    /* Add the vendor specific attributes for the NL command. */
    nlData = wifiLoggerCommand->attr_start(NL80211_ATTR_VENDOR_DATA);

    if (!nlData)
        goto cleanup;

    if (wifiLoggerCommand->put_u32(
            QCA_WLAN_VENDOR_ATTR_WIFI_INFO_DRIVER_VERSION, requestId))
    {
        goto cleanup;
    }
    wifiLoggerCommand->attr_end(nlData);

    wifiLoggerCommand->setVersionInfo(buffer, buffer_size);

    /* Send the msg and wait for a response. */
    ret = wifiLoggerCommand->requestResponse();
    if (ret) {
        ALOGE("%s: Error %d happened. ", __FUNCTION__, ret);
    }
cleanup:
    delete wifiLoggerCommand;
    return (wifi_error)ret;
}


/* Function to get the Firmware memory dump. */
wifi_error wifi_get_firmware_memory_dump(wifi_interface_handle iface,
                                wifi_firmware_memory_dump_handler handler)
{
    int requestId, ret = 0;
    WifiLoggerCommand *wifiLoggerCommand;
    struct nlattr *nlData;
    interface_info *ifaceInfo = getIfaceInfo(iface);
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    /* No request id from caller, so generate one and pass it on to the driver.
     * Generate one randomly.
     */
    requestId = rand();

    wifiLoggerCommand = new WifiLoggerCommand(
                            wifiHandle,
                            requestId,
                            OUI_QCA,
                            QCA_NL80211_VENDOR_SUBCMD_WIFI_LOGGER_MEMORY_DUMP);
    if (wifiLoggerCommand == NULL) {
        ALOGE("%s: Error WifiLoggerCommand NULL", __FUNCTION__);
        return WIFI_ERROR_UNKNOWN;
    }
    /* Create the NL message. */
    ret = wifiLoggerCommand->create();

    if (ret < 0)
        goto cleanup;

    /* Set the interface Id of the message. */
    ret = wifiLoggerCommand->set_iface_id(ifaceInfo->name);

    if (ret < 0)
        goto cleanup;

    /* Add the vendor specific attributes for the NL command. */
    nlData = wifiLoggerCommand->attr_start(NL80211_ATTR_VENDOR_DATA);

    if (!nlData)
        goto cleanup;

    wifiLoggerCommand->attr_end(nlData);

    /* copy the callback into callback handler */
    WifiLoggerCallbackHandler callbackHandler;
    memset(&callbackHandler, 0, sizeof(callbackHandler));
    callbackHandler.on_firmware_memory_dump = \
        handler.on_firmware_memory_dump;

    ret = wifiLoggerCommand->setCallbackHandler(callbackHandler);
    if (ret < 0)
        goto cleanup;

    /* Send the msg and wait for the memory dump event */
    wifiLoggerCommand->waitForRsp(true);
    ret = wifiLoggerCommand->requestEvent();
    if (ret) {
        ALOGE("%s: Error %d happened. ", __FUNCTION__, ret);
    }

cleanup:
    delete wifiLoggerCommand;
    return (wifi_error)ret;
}

wifi_error wifi_set_log_handler(wifi_request_id id,
                                wifi_interface_handle iface,
                                wifi_ring_buffer_data_handler handler)
{
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    info->on_ring_buffer_data = handler.on_ring_buffer_data;
    if (handler.on_ring_buffer_data == NULL) {
        ALOGE("Input handler is NULL");
        return WIFI_ERROR_UNKNOWN;
    }
    return WIFI_SUCCESS;
}

wifi_error wifi_reset_log_handler(wifi_request_id id,
                                  wifi_interface_handle iface)
{
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    /* Some locking needs to be introduced here */
    info->on_ring_buffer_data = NULL;
    return WIFI_SUCCESS;
}

wifi_error wifi_set_alert_handler(wifi_request_id id,
                                  wifi_interface_handle iface,
                                  wifi_alert_handler handler)
{
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    if (handler.on_alert) {
        ALOGE("Input handler is NULL");
        return WIFI_ERROR_UNKNOWN;
    }
    //TODO check locking
    info->on_alert = handler.on_alert;
    return WIFI_SUCCESS;
}

wifi_error wifi_reset_alert_handler(wifi_request_id id,
                                    wifi_interface_handle iface)
{
    wifi_handle wifiHandle = getWifiHandle(iface);
    hal_info *info = getHalInfo(wifiHandle);

    /* Some locking needs to be introduced here */
    info->on_alert = NULL;
    return WIFI_SUCCESS;
}

WifiLoggerCommand::WifiLoggerCommand(wifi_handle handle, int id, u32 vendor_id, u32 subcmd)
        : WifiVendorCommand(handle, id, vendor_id, subcmd)
{
    ALOGV("WifiLoggerCommand %p constructed", this);
    mVersion = NULL;
    mVersionLen = 0;
    mRequestId = id;
    memset(&mHandler, 0,sizeof(mHandler));
    mWaitforRsp = false;
    mMoreData = false;
    mSupportedSet = NULL;
}

WifiLoggerCommand::~WifiLoggerCommand()
{
    ALOGD("WifiLoggerCommand %p destructor", this);
    unregisterVendorHandler(mVendor_id, mSubcmd);
}

/* This function implements creation of Vendor command */
int WifiLoggerCommand::create() {
    int ret = mMsg.create(NL80211_CMD_VENDOR, 0, 0);
    if (ret < 0) {
        return ret;
    }

    /* Insert the oui in the msg */
    ret = mMsg.put_u32(NL80211_ATTR_VENDOR_ID, mVendor_id);
    if (ret < 0)
        goto out;
    /* Insert the subcmd in the msg */
    ret = mMsg.put_u32(NL80211_ATTR_VENDOR_SUBCMD, mSubcmd);
    if (ret < 0)
        goto out;

     ALOGI("%s: mVendor_id = %d, Subcmd = %d.",
        __FUNCTION__, mVendor_id, mSubcmd);

out:
    return ret;
}

void rb_timerhandler(hal_info *info)
{
   struct timeval now;
   int rb_id;

   gettimeofday(&now,NULL);
   for (rb_id = 0; rb_id < NUM_RING_BUFS; rb_id++) {
       rb_check_for_timeout(&info->rb_infos[rb_id], &now);
   }
}

wifi_error wifi_logger_ring_buffers_init(hal_info *info)
{
    wifi_error ret;

    ret = rb_init(info, &info->rb_infos[POWER_EVENTS_RB_ID],
                  POWER_EVENTS_RB_ID,
                  POWER_EVENTS_RB_BUF_SIZE,
                  POWER_EVENTS_NUM_BUFS,
                  power_events_ring_name);
    if (ret != WIFI_SUCCESS) {
        ALOGE("Failed to initialize power events ring buffer");
        goto cleanup;
    }

    ret = rb_init(info, &info->rb_infos[CONNECTIVITY_EVENTS_RB_ID],
                  CONNECTIVITY_EVENTS_RB_ID,
                  CONNECTIVITY_EVENTS_RB_BUF_SIZE,
                  CONNECTIVITY_EVENTS_NUM_BUFS,
                  connectivity_events_ring_name);
    if (ret != WIFI_SUCCESS) {
        ALOGE("Failed to initialize connectivity events ring buffer");
        goto cleanup;
    }

    ret = rb_init(info, &info->rb_infos[PKT_STATS_RB_ID],
                  PKT_STATS_RB_ID,
                  PKT_STATS_RB_BUF_SIZE,
                  PKT_STATS_NUM_BUFS,
                  pkt_stats_ring_name);
    if (ret != WIFI_SUCCESS) {
        ALOGE("Failed to initialize per packet stats ring buffer");
        goto cleanup;
    }

    ret = rb_init(info, &info->rb_infos[DRIVER_PRINTS_RB_ID],
                  DRIVER_PRINTS_RB_ID,
                  DRIVER_PRINTS_RB_BUF_SIZE,
                  DRIVER_PRINTS_NUM_BUFS,
                  driver_prints_ring_name);
    if (ret != WIFI_SUCCESS) {
        ALOGE("Failed to initialize driver prints ring buffer");
        goto cleanup;
    }

    ret = rb_init(info, &info->rb_infos[FIRMWARE_PRINTS_RB_ID],
                  FIRMWARE_PRINTS_RB_ID,
                  FIRMWARE_PRINTS_RB_BUF_SIZE,
                  FIRMWARE_PRINTS_NUM_BUFS,
                  firmware_prints_ring_name);
    if (ret != WIFI_SUCCESS) {
        ALOGE("Failed to initialize firmware prints ring buffer");
        goto cleanup;
    }

    return ret;

cleanup:
    wifi_logger_ring_buffers_deinit(info);
    return ret;
}

void wifi_logger_ring_buffers_deinit(hal_info *info)
{
    int i;

    for (i = 0; i < NUM_RING_BUFS; i++) {
        rb_deinit(&info->rb_infos[i]);
    }
}


/* Callback handlers registered for nl message send */
static int error_handler_wifi_logger(struct sockaddr_nl *nla,
                                     struct nlmsgerr *err,
                                     void *arg)
{
    struct sockaddr_nl *tmp;
    int *ret = (int *)arg;
    tmp = nla;
    *ret = err->error;
    ALOGE("%s: Error code:%d (%s)", __FUNCTION__, *ret, strerror(-(*ret)));
    return NL_STOP;
}

/* Callback handlers registered for nl message send */
static int ack_handler_wifi_logger(struct nl_msg *msg, void *arg)
{
    int *ret = (int *)arg;
    struct nl_msg * a;

    ALOGE("%s: called", __FUNCTION__);
    a = msg;
    *ret = 0;
    return NL_STOP;
}

/* Callback handlers registered for nl message send */
static int finish_handler_wifi_logger(struct nl_msg *msg, void *arg)
{
  int *ret = (int *)arg;
  struct nl_msg * a;

  ALOGE("%s: called", __FUNCTION__);
  a = msg;
  *ret = 0;
  return NL_SKIP;
}

int WifiLoggerCommand::requestEvent()
{
    int res = -1;
    struct nl_cb *cb;

    ALOGD("%s: Entry.", __FUNCTION__);

    cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (!cb) {
        ALOGE("%s: Callback allocation failed",__FUNCTION__);
        res = -1;
        goto out;
    }

    /* Send message */
    res = nl_send_auto_complete(mInfo->cmd_sock, mMsg.getMessage());
    if (res < 0)
        goto out;
    res = 1;

    nl_cb_err(cb, NL_CB_CUSTOM, error_handler_wifi_logger, &res);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler_wifi_logger, &res);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler_wifi_logger, &res);

    /* Err is populated as part of finish_handler. */
    while (res > 0){
         nl_recvmsgs(mInfo->cmd_sock, cb);
    }

    ALOGD("%s: Msg sent, res=%d, mWaitForRsp=%d", __FUNCTION__, res, mWaitforRsp);
    /* Only wait for the asynchronous event if HDD returns success, res=0 */
    if (!res && (mWaitforRsp == true)) {
        struct timespec abstime;
        abstime.tv_sec = 4;
        abstime.tv_nsec = 0;
        res = mCondition.wait(abstime);
        if (res == ETIMEDOUT)
        {
            ALOGE("%s: Time out happened.", __FUNCTION__);
        }
        ALOGD("%s: Command invoked return value:%d, mWaitForRsp=%d",
            __FUNCTION__, res, mWaitforRsp);
    }
out:
    /* Cleanup the mMsg */
    mMsg.destroy();
    return res;
}

int WifiLoggerCommand::requestResponse()
{
    return WifiCommand::requestResponse(mMsg);
}

int WifiLoggerCommand::handleResponse(WifiEvent &reply) {
    ALOGD("Received a WifiLogger response message from Driver");
    u32 status;
    int ret = WIFI_SUCCESS;
    int i = 0;
    int len = 0, version;
    char version_type[20];
    WifiVendorCommand::handleResponse(reply);

    switch(mSubcmd)
    {
        case QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO:
        {
            struct nlattr *tb_vendor[QCA_WLAN_VENDOR_ATTR_WIFI_INFO_GET_MAX + 1];

            nla_parse(tb_vendor, QCA_WLAN_VENDOR_ATTR_WIFI_INFO_GET_MAX,
                            (struct nlattr *)mVendorData, mDataLen, NULL);

            if (tb_vendor[QCA_WLAN_VENDOR_ATTR_WIFI_INFO_DRIVER_VERSION]) {
                len = nla_len(tb_vendor[
                        QCA_WLAN_VENDOR_ATTR_WIFI_INFO_DRIVER_VERSION]);
                memcpy(version_type, "Driver", strlen("Driver"));
                version = QCA_WLAN_VENDOR_ATTR_WIFI_INFO_DRIVER_VERSION;
            } else if (
                tb_vendor[QCA_WLAN_VENDOR_ATTR_WIFI_INFO_FIRMWARE_VERSION]) {
                len = nla_len(
                        tb_vendor[
                        QCA_WLAN_VENDOR_ATTR_WIFI_INFO_FIRMWARE_VERSION]);
                memcpy(version_type, "Firmware", strlen("Firmware"));
                version = QCA_WLAN_VENDOR_ATTR_WIFI_INFO_FIRMWARE_VERSION;
            }
            if (len && mVersion && mVersionLen) {
                memset(mVersion, 0, mVersionLen);
                /* if len is greater than the incoming length then
                   accommodate 1 lesser than mVersionLen to have the
                   string terminated with '\0' */
                len = (len > mVersionLen)? (mVersionLen - 1) : len;
                memcpy(mVersion, nla_data(tb_vendor[version]), len);
                ALOGD("%s: WLAN version len : %d", __FUNCTION__, len);
                ALOGD("%s: WLAN %s version : %s ", __FUNCTION__,
                      version_type, mVersion);
            }
        }
        break;
        case QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE_SET:
        {
            struct nlattr *tb_vendor[QCA_WLAN_VENDOR_ATTR_FEATURE_SET_MAX + 1];

            nla_parse(tb_vendor, QCA_WLAN_VENDOR_ATTR_FEATURE_SET_MAX,
                            (struct nlattr *)mVendorData, mDataLen, NULL);

            if (tb_vendor[QCA_WLAN_VENDOR_ATTR_FEATURE_SET]) {
                *mSupportedSet =
                nla_get_u32(tb_vendor[QCA_WLAN_VENDOR_ATTR_FEATURE_SET]);
                ALOGD("%s: Supported Feature Set : val 0x%x",
                      __FUNCTION__, *mSupportedSet);
            }
        }
        break;
        default :
            ALOGE("%s: Wrong Wifi Logger subcmd response received %d",
                __FUNCTION__, mSubcmd);
    }

    return NL_SKIP;
}

/* This function will be the main handler for incoming (from driver)
 * WIFI_LOGGER_SUBCMD.
 * Calls the appropriate callback handler after parsing the vendor data.
 */
int WifiLoggerCommand::handleEvent(WifiEvent &event)
{
    unsigned i = 0;
    u32 status;
    int ret = WIFI_SUCCESS;
    char* memBuffer = NULL;
    FILE* memDumpFilePtr = NULL;

    WifiVendorCommand::handleEvent(event);

    struct nlattr *tbVendor[
        QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_MAX + 1];
    nla_parse(tbVendor, QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_MAX,
            (struct nlattr *)mVendorData,
            mDataLen, NULL);

    switch(mSubcmd)
    {
        case QCA_NL80211_VENDOR_SUBCMD_WIFI_LOGGER_MEMORY_DUMP:
        {
            int id = 0;
            u32 memDumpSize = 0;
            int numRecordsRead = 0;
            u32 remaining = 0;
            char* buffer = NULL;

            if (!tbVendor[
                QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_REQUEST_ID]) {
                ALOGE("%s: LOGGER_RESULTS_REQUEST_ID not"
                    "found, continuing...", __func__);
            }
            else {
                id = nla_get_u32(tbVendor[
                          QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_REQUEST_ID]
                      );
                ALOGI("%s: Event has Req. ID:%d, ours:%d",
                    __func__, id, mRequestId);
            }

            if (!tbVendor[
                QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_MEMDUMP_SIZE]) {
                ALOGE("%s: LOGGER_RESULTS_MEMDUMP_SIZE not"
                    "found", __func__);
                break;
            }

            memDumpSize = nla_get_u32(
                tbVendor[QCA_WLAN_VENDOR_ATTR_LOGGER_RESULTS_MEMDUMP_SIZE]
                );

            /* Allocate the memory indicated in memDumpSize */
            memBuffer = (char*) malloc(sizeof(char) * memDumpSize);
            if (memBuffer == NULL) {
                ALOGE("%s: No Memory for allocating Buffer ",
                      "size of %d", __func__, memDumpSize);
                break;
            }
            memset(memBuffer, 0, sizeof(char) * memDumpSize);

            ALOGI("%s: Memory Dump size: %u", __func__,
                  memDumpSize);

            /* Open the proc or debugfs filesystem */
            memDumpFilePtr = fopen(LOGGER_MEMDUMP_FILENAME, "r");
            if (memDumpFilePtr == NULL) {
                ALOGE("Failed to open %s file", LOGGER_MEMDUMP_FILENAME);
                break;
            }

            /* Read the memDumpSize value at once */
            numRecordsRead = fread(memBuffer, 1, memDumpSize,
                                   memDumpFilePtr);
            if (numRecordsRead <= 0 ||
                numRecordsRead != (int) memDumpSize) {
                ALOGE("%s: Read %d failed for reading at once.",
                      __func__, numRecordsRead);
                /* Lets try to read in chunks */
                rewind(memDumpFilePtr);
                remaining = memDumpSize;
                buffer = memBuffer;
                while (remaining) {
                    u32 readSize = 0;
                    if (remaining >= LOGGER_MEMDUMP_CHUNKSIZE) {
                        readSize = LOGGER_MEMDUMP_CHUNKSIZE;
                    }
                    else {
                        readSize = remaining;
                    }
                    numRecordsRead = fread(buffer, 1,
                                           readSize, memDumpFilePtr);
                    if (numRecordsRead) {
                        remaining -= readSize;
                        buffer += readSize;
                        ALOGI("%s: Read successful for size:%u "
                              "remaining:%u", __func__, readSize,
                              remaining);
                    }
                    else {
                        ALOGE("%s: Chunk read failed for size:%u",
                              __func__, readSize);
                        break;
                    }
                }
            }

            /* After successful read, call the callback handler*/
            if (mHandler.on_firmware_memory_dump) {
                mHandler.on_firmware_memory_dump(memBuffer,
                                                 memDumpSize);

            }
        }
        break;

       default:
           /* Error case should not happen print log */
           ALOGE("%s: Wrong subcmd received %d", __func__, mSubcmd);
           break;
    }

cleanup:
    /* free the allocated memory */
    if (memBuffer) {
        free(memBuffer);
    }
    return NL_SKIP;
}

int WifiLoggerCommand::setCallbackHandler(WifiLoggerCallbackHandler nHandler)
{
    int res = 0;
    mHandler = nHandler;
    res = registerVendorHandler(mVendor_id, mSubcmd);
    if (res != 0) {
        ALOGE("%s: Unable to register Vendor Handler Vendor Id=0x%x subcmd=%u",
              __FUNCTION__, mVendor_id, mSubcmd);
    }
    return res;
}

void WifiLoggerCommand::unregisterHandler(u32 subCmd)
{
    unregisterVendorHandler(mVendor_id, subCmd);
}

int WifiLoggerCommand::timed_wait(u16 wait_time)
{
    struct timespec absTime;
    int res;
    absTime.tv_sec = wait_time;
    absTime.tv_nsec = 0;
    return mCondition.wait(absTime);
}

void WifiLoggerCommand::waitForRsp(bool wait)
{
    mWaitforRsp = wait;
}

