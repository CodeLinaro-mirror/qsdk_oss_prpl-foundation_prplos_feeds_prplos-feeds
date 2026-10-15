/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#define _GNU_SOURCE

#include <amxc/amxc.h>
#include <amxp/amxp.h>
#include <amxd/amxd_action.h>
#include <amxd/amxd_function.h>
#include <amxd/amxd_object.h>
#include <amxd/amxd_object_event.h>
#include <amxd/amxd_object_parameter.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_parameter.h>
#include <amxo/amxo.h>
#include <debug/sahtrace.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define ME "energyservice"
#define CONFIG_FILE "/tmp/gw_energy_app.conf"
#define CONFIG_TMP_FILE "/tmp/gw_energy_app.conf.tmp"
#define WORKER_INIT "/etc/init.d/gw_energy_app_prpl"
#define ERP_CLI_ADDRESS "127.0.0.1"
#define ERP_CLI_PORT 7785
#define ERP_CLI_TIMEOUT_SEC 2
#define ERP_CLI_MAX_COMMAND_LENGTH 1024
#define ERP_CLI_RESPONSE_LENGTH 4096
#define ERP_STATUS_OUTPUT_LENGTH 1024
#define ENERGY_EXPORT __attribute__((visibility("default")))

typedef enum {
    VALUE_UINT,
    VALUE_BOOL,
    VALUE_STRING,
} value_type_t;

typedef struct {
    const char* name;
    value_type_t type;
} config_param_t;

typedef struct {
    char status[64];
    char avg_enter_time[32];
    char avg_exit_time[32];
    uint32_t enter_count;
    uint32_t exit_count;
} erp_status_row_t;

static amxd_dm_t* energy_dm;

static const config_param_t config_params[] = {
    {"DbgLevels", VALUE_STRING},
    {"Respawn", VALUE_BOOL},
    {"WlanIfacesNotUpWait", VALUE_UINT},
    {"LogToFile", VALUE_BOOL},
    {"LogFileSize", VALUE_UINT},
};

static const config_param_t wlanif_params[] = {
    {"AppInitSendDelay", VALUE_UINT},
    {"AppInitRetryTimeout", VALUE_UINT},
    {"AppInitMaxRetries", VALUE_UINT},
    {"NetlinkVdevInterfacePrefix", VALUE_STRING},
    {"NetlinkPdevInterfacePrefix", VALUE_STRING},
    {"NetlinkInterfacePrefixCompareLength", VALUE_STRING},
};

static const config_param_t energy_params[] = {
    {"Enable", VALUE_BOOL},
    {"DbgLevels", VALUE_STRING},
    {"EnergyServiceMonitoringTimeout", VALUE_UINT},
    {"EnergyServiceCoolingPeriodToReduceBWorChain", VALUE_UINT},
    {"EnergyServiceMinimumAvailableAirtimeToReduceBWorChain", VALUE_UINT},
    {"EnergyServiceTriggerAirtimeUtilThreshold", VALUE_UINT},
    {"EnergyServiceTriggerResourceExpansionAirtimeUtilThreshold", VALUE_UINT},
    {"EnergyServiceMinimumBwToChange", VALUE_UINT},
    {"EnergyServiceMinimumTxChainToChange", VALUE_UINT},
    {"EnergyServiceMinimumRxChainToChange", VALUE_UINT},
    {"EnergyServiceMinimumRequestedBwToChange", VALUE_UINT},
    {"EnergyServiceMinimumRequestedTxChainToChange", VALUE_UINT},
    {"EnergyServiceMinimumRequestedRxChainToChange", VALUE_UINT},
    {"EnergyServiceMinimumClientPhymode", VALUE_UINT},
    {"EnergyServiceUseOnlyRequestedConfig", VALUE_BOOL},
    {"EnergyServiceLCEnable", VALUE_BOOL},
    {"EnergyServiceLCLinkBringUpAAThresh", VALUE_UINT},
    {"EnergyServiceLCLinkBringDownAAThresh", VALUE_UINT},
    {"EnergyServiceLCLinkBringDownPeerNumThresh", VALUE_UINT},
    {"EnergyServiceLCType", VALUE_UINT},
    {"EnergyServiceLCAlwaysUpBand", VALUE_STRING},
    {"EnergyServiceLCMonitoringTimeout", VALUE_UINT},
    {"EnergyServiceAlgoMode", VALUE_UINT},
    {"EnergyServicePredominantMcsThreshold", VALUE_UINT},
    {"EnergyServicePredominantBwThreshold", VALUE_UINT},
    {"EnergyServiceMaxThroughputThreshold20Mhz", VALUE_UINT},
    {"EnergyServiceMaxThroughputThreshold40Mhz", VALUE_UINT},
    {"EnergyServiceMaxThroughputThreshold80Mhz", VALUE_UINT},
    {"EnergyServiceMinMcsForTxReduction", VALUE_UINT},
    {"EnergyServiceLicenseEnabled", VALUE_BOOL},
};

static const config_param_t erp_params[] = {
    {"DbgLevels", VALUE_STRING},
    {"IdlePollPeriod", VALUE_UINT},
    {"erp_mode", VALUE_UINT},
    {"pon_mode", VALUE_UINT},
    {"is_nss_standby_disabled", VALUE_BOOL},
    {"is_pon_standby_disabled", VALUE_BOOL},
    {"is_wifi_standby_disabled", VALUE_BOOL},
    {"is_bsp_standby_disabled", VALUE_BOOL},
    {"is_green_ap_disabled", VALUE_BOOL},
};

static amxd_object_t* energy_root(void) {
    return energy_dm == NULL ? NULL : amxd_dm_findf(energy_dm, "EnergyService.");
}

static amxd_object_t* child_object(amxd_object_t* root, const char* name) {
    return root == NULL || name == NULL ? NULL : amxd_object_findf(root, "%s", name);
}

static bool parse_u32_token(const char* token, uint32_t* value) {
    char* end = NULL;
    unsigned long parsed;

    if(token == NULL || value == NULL || *token == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtoul(token, &end, 10);
    if(errno != 0 || end == token || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t) parsed;
    return true;
}

/*
 * The Energy App prints each row as:
 *
 *   <system> <status> <enter value> <enter unit> <exit value> <exit unit>
 *           <enter count> <exit count>
 *
 * The status itself can contain spaces, so the last six fields are parsed
 * from the right-hand side and everything before them is retained as status.
 */
static bool parse_erp_status_row(const char* line,
                                 const char* system,
                                 erp_status_row_t* row) {
    char fields[256];
    char* tokens[32];
    char* save = NULL;
    char* token;
    const char* rest;
    size_t count = 0;
    size_t used = 0;

    if(line == NULL || system == NULL || row == NULL) {
        return false;
    }
    while(isspace((unsigned char) *line)) {
        line++;
    }
    /* The ERP CLI can prefix the first table line with its prompt. */
    if(*line == '@') {
        line++;
        while(isspace((unsigned char) *line)) {
            line++;
        }
    }
    if(strncmp(line, system, strlen(system)) != 0) {
        return false;
    }
    rest = line + strlen(system);
    if(*rest != '\0' && !isspace((unsigned char) *rest)) {
        return false;
    }
    while(isspace((unsigned char) *rest)) {
        rest++;
    }
    if(*rest == '\0') {
        return false;
    }

    snprintf(fields, sizeof(fields), "%s", rest);
    token = strtok_r(fields, " \t", &save);
    while(token != NULL && count < (sizeof(tokens) / sizeof(tokens[0]))) {
        tokens[count++] = token;
        token = strtok_r(NULL, " \t", &save);
    }
    if(token != NULL || count < 7) {
        return false;
    }

    if(!parse_u32_token(tokens[count - 2], &row->enter_count) ||
       !parse_u32_token(tokens[count - 1], &row->exit_count)) {
        return false;
    }

    row->status[0] = '\0';
    for(size_t i = 0; i < count - 6; i++) {
        int written = snprintf(row->status + used,
                               sizeof(row->status) - used,
                               "%s%s", used == 0 ? "" : " ", tokens[i]);
        if(written < 0 || (size_t) written >= sizeof(row->status) - used) {
            return false;
        }
        used += (size_t) written;
    }

    if(snprintf(row->avg_enter_time, sizeof(row->avg_enter_time), "%s %s",
                tokens[count - 6], tokens[count - 5]) >=
       (int) sizeof(row->avg_enter_time) ||
       snprintf(row->avg_exit_time, sizeof(row->avg_exit_time), "%s %s",
                tokens[count - 4], tokens[count - 3]) >=
       (int) sizeof(row->avg_exit_time)) {
        return false;
    }
    return true;
}

static bool parse_erp_status_buffer(const char* buffer,
                                    size_t length,
                                    erp_status_row_t rows[4]) {
    static const char* const systems[] = {"Overall", "NSS SS", "WiFi SS", "BSP SS"};
    erp_status_row_t parsed[4] = {{0}};
    bool found[4] = {false, false, false, false};
    char text[16384];
    char* save = NULL;
    char* line;
    bool complete = true;

    if(buffer == NULL || rows == NULL || length >= sizeof(text)) {
        return false;
    }
    memcpy(text, buffer, length);
    text[length] = '\0';

    line = strtok_r(text, "\r\n", &save);
    while(line != NULL) {
        for(size_t i = 0; i < sizeof(systems) / sizeof(systems[0]); i++) {
            if(!found[i] && parse_erp_status_row(line, systems[i], &parsed[i])) {
                found[i] = true;
            }
        }
        line = strtok_r(NULL, "\r\n", &save);
    }

    for(size_t i = 0; i < sizeof(found) / sizeof(found[0]); i++) {
        if(!found[i]) {
            complete = false;
            break;
        }
    }
    if(complete) {
        memcpy(rows, parsed, sizeof(parsed));
    }
    return complete;
}

static bool query_erp_status(erp_status_row_t rows[4]) {
    struct sockaddr_in address = {0};
    struct timeval timeout = {.tv_sec = ERP_CLI_TIMEOUT_SEC, .tv_usec = 0};
    const char command[] = "erp cli status\r\n";
    char response[sizeof(char) * 16384];
    size_t used = 0;
    int fd = -1;
    bool complete = false;

    if(rows == NULL) {
        return false;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        SAH_TRACEZ_WARNING(ME, "Cannot create ERP CLI socket: %s", strerror(errno));
        return false;
    }
    (void) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    address.sin_family = AF_INET;
    address.sin_port = htons(ERP_CLI_PORT);
    if(inet_pton(AF_INET, ERP_CLI_ADDRESS, &address.sin_addr) != 1 ||
       connect(fd, (struct sockaddr*) &address, sizeof(address)) != 0) {
        SAH_TRACEZ_WARNING(ME, "Cannot connect to ERP CLI %s:%u: %s",
                           ERP_CLI_ADDRESS, ERP_CLI_PORT, strerror(errno));
        close(fd);
        return false;
    }

    for(size_t sent = 0; sent < sizeof(command) - 1;) {
        ssize_t result = send(fd, command + sent, sizeof(command) - 1 - sent, 0);
        if(result <= 0) {
            SAH_TRACEZ_WARNING(ME, "Cannot send ERP CLI status command: %s", strerror(errno));
            close(fd);
            return false;
        }
        sent += (size_t) result;
    }
    while(used < sizeof(response) - 1) {
        ssize_t result = recv(fd, response + used, sizeof(response) - 1 - used, 0);
        if(result <= 0) {
            break;
        }
        used += (size_t) result;
        if(parse_erp_status_buffer(response, used, rows)) {
            complete = true;
            break;
        }
    }
    if(!complete) {
        complete = parse_erp_status_buffer(response, used, rows);
    }
    close(fd);
    if(!complete) {
        SAH_TRACEZ_WARNING(ME, "ERP CLI returned no complete status response");
    }
    return complete;
}

typedef enum {
    ERP_CLI_RESULT_OK,
    ERP_CLI_RESULT_INVALID,
    ERP_CLI_RESULT_ERROR,
} erp_cli_result_t;

static bool prepare_erp_cli_command(const char* input,
                                    const char* expected_prefix,
                                    char command[ERP_CLI_MAX_COMMAND_LENGTH]) {
    const char* start;
    size_t input_length;
    size_t command_length;
    size_t prefix_length;

    if(input == NULL || expected_prefix == NULL || command == NULL) {
        return false;
    }
    input_length = strlen(input);
    if(input_length == 0 || input_length >= ERP_CLI_MAX_COMMAND_LENGTH) {
        return false;
    }
    for(size_t i = 0; i < input_length; i++) {
        if(input[i] == '\r' || input[i] == '\n') {
            return false;
        }
    }

    start = input;
    while(isspace((unsigned char) *start)) {
        start++;
    }
    command_length = input_length - (size_t) (start - input);
    while(command_length > 0 && isspace((unsigned char) start[command_length - 1])) {
        command_length--;
    }
    if(command_length == 0) {
        return false;
    }

    prefix_length = strlen(expected_prefix);
    if(command_length < prefix_length ||
       strncmp(start, expected_prefix, prefix_length) != 0 ||
       (command_length > prefix_length &&
        !isspace((unsigned char) start[prefix_length]))) {
        return false;
    }
    if(command_length + 2 >= ERP_CLI_MAX_COMMAND_LENGTH) {
        return false;
    }
    memcpy(command, start, command_length);
    command[command_length++] = '\r';
    command[command_length++] = '\n';
    command[command_length] = '\0';
    return true;
}

static bool erp_cli_response_has_error(const char* response) {
    static const char* const error_markers[] = {
        "invalid", "unknown", "error", "failed", "failure",
        "unsupported", "not supported", "usage"
    };

    if(response == NULL) {
        return false;
    }
    for(size_t i = 0; i < sizeof(error_markers) / sizeof(error_markers[0]); i++) {
        if(strcasestr(response, error_markers[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static erp_cli_result_t send_erp_cli_command(const char* input,
                                             const char* expected_prefix) {
    struct sockaddr_in address = {0};
    struct timeval timeout = {.tv_sec = ERP_CLI_TIMEOUT_SEC, .tv_usec = 0};
    char command[ERP_CLI_MAX_COMMAND_LENGTH];
    char response[ERP_CLI_RESPONSE_LENGTH];
    size_t command_length;
    size_t used = 0;
    int fd = -1;

    if(!prepare_erp_cli_command(input, expected_prefix, command)) {
        SAH_TRACEZ_WARNING(ME, "Invalid ERP CLI command: %s", input == NULL ? "(null)" : input);
        return ERP_CLI_RESULT_INVALID;
    }
    command_length = strlen(command);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        SAH_TRACEZ_WARNING(ME, "Cannot create ERP CLI socket for '%s': %s",
                           command, strerror(errno));
        return ERP_CLI_RESULT_ERROR;
    }
    (void) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void) setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    address.sin_family = AF_INET;
    address.sin_port = htons(ERP_CLI_PORT);
    if(inet_pton(AF_INET, ERP_CLI_ADDRESS, &address.sin_addr) != 1 ||
       connect(fd, (struct sockaddr*) &address, sizeof(address)) != 0) {
        SAH_TRACEZ_WARNING(ME, "Cannot connect to ERP CLI %s:%u for '%s': %s",
                           ERP_CLI_ADDRESS, ERP_CLI_PORT, command, strerror(errno));
        close(fd);
        return ERP_CLI_RESULT_ERROR;
    }

    for(size_t sent = 0; sent < command_length;) {
        ssize_t result = send(fd, command + sent, command_length - sent, 0);
        if(result <= 0) {
            SAH_TRACEZ_WARNING(ME, "Cannot send ERP CLI command '%s': %s",
                               command, strerror(errno));
            close(fd);
            return ERP_CLI_RESULT_ERROR;
        }
        sent += (size_t) result;
    }

    while(used < sizeof(response) - 1) {
        ssize_t result = recv(fd, response + used, sizeof(response) - 1 - used, 0);
        if(result <= 0) {
            if(result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                SAH_TRACEZ_WARNING(ME, "Cannot read ERP CLI response for '%s': %s",
                                   command, strerror(errno));
                close(fd);
                return ERP_CLI_RESULT_ERROR;
            }
            break;
        }
        used += (size_t) result;
    }
    response[used] = '\0';
    close(fd);

    if(erp_cli_response_has_error(response)) {
        SAH_TRACEZ_WARNING(ME, "ERP CLI rejected command '%s': %s", command, response);
        return ERP_CLI_RESULT_INVALID;
    }
    SAH_TRACEZ_INFO(ME, "ERP CLI command accepted: %s", command);
    return ERP_CLI_RESULT_OK;
}

static bool refresh_erp_status(erp_status_row_t rows[4]) {
    if(rows == NULL) {
        return false;
    }
    if(!query_erp_status(rows)) {
        SAH_TRACEZ_WARNING(ME, "ERP status refresh failed while querying the CLI");
        return false;
    }
    SAH_TRACEZ_INFO(ME, "ERP status refresh completed");
    return true;
}

static bool format_erp_status(const erp_status_row_t rows[4],
                              char output[ERP_STATUS_OUTPUT_LENGTH]) {
    static const char* const systems[] = {"Overall", "NSS SS", "WiFi SS", "BSP SS"};
    int written;

    if(rows == NULL || output == NULL) {
        return false;
    }
    written = snprintf(output, ERP_STATUS_OUTPUT_LENGTH,
                       "%-20s%-21s%-17s%-17s%-13s%s\n",
                       "System", "Status", "Avg Enter time", "Avg Exit time",
                       "Enter Count", "Exit Count");
    if(written < 0 || written >= ERP_STATUS_OUTPUT_LENGTH) {
        return false;
    }
    for(size_t i = 0; i < 4; i++) {
        int added = snprintf(output + written,
                             ERP_STATUS_OUTPUT_LENGTH - (size_t) written,
                             "%-20s%-21s%-17s%-17s%-13u%u\n",
                             systems[i], rows[i].status, rows[i].avg_enter_time,
                             rows[i].avg_exit_time, rows[i].enter_count,
                             rows[i].exit_count);
        if(added < 0 || (size_t) added >= ERP_STATUS_OUTPUT_LENGTH - (size_t) written) {
            return false;
        }
        written += added;
    }
    return true;
}

static bool write_param(FILE* file, amxd_object_t* object, const config_param_t* param) {
    const amxc_var_t* value;

    if(file == NULL || object == NULL || param == NULL) {
        return false;
    }
    value = amxd_object_get_param_value(object, param->name);
    if(value == NULL) {
        SAH_TRACEZ_ERROR(ME, "Missing DM parameter %s", param->name);
        return false;
    }

    switch(param->type) {
    case VALUE_BOOL:
        fprintf(file, "%s=%u\n", param->name,
                amxc_var_dyncast(bool, value) ? 1u : 0u);
        break;
    case VALUE_UINT:
        fprintf(file, "%s=%u\n", param->name,
                amxc_var_dyncast(uint32_t, value));
        break;
    case VALUE_STRING:
        fprintf(file, "%s=%s\n", param->name,
                amxc_var_constcast(cstring_t, value));
        break;
    }
    return true;
}

static bool write_section(FILE* file, amxd_object_t* root, const char* section,
                          const char* child, const config_param_t* params,
                          size_t count) {
    amxd_object_t* object = child_object(root, child);

    if(object == NULL) {
        SAH_TRACEZ_ERROR(ME, "Missing DM object EnergyService.%s", child);
        return false;
    }
    fprintf(file, "[%s]\n", section);
    for(size_t i = 0; i < count; i++) {
        if(!write_param(file, object, &params[i])) {
            return false;
        }
    }
    fputc('\n', file);
    return true;
}

static bool write_config(amxd_object_t* root) {
    FILE* file;
    bool ok;

    if(root == NULL) {
        return false;
    }
    file = fopen(CONFIG_TMP_FILE, "w");
    if(file == NULL) {
        SAH_TRACEZ_ERROR(ME, "Cannot open %s: %s", CONFIG_TMP_FILE, strerror(errno));
        return false;
    }

    fprintf(file, ";\n"
                 ";  Automatically generated Energy App config file. Do not change it.\n"
                 ";\n"
                 ";CONFIG : Application configuration\n"
                 ";WLANIF : List of wlan interfaces\n"
                 ";Energy : Energy App\n\n");

    ok = write_section(file, root, "CONFIG", "Config", config_params,
                       sizeof(config_params) / sizeof(config_params[0])) &&
         write_section(file, root, "WLANIF", "Wlanif", wlanif_params,
                       sizeof(wlanif_params) / sizeof(wlanif_params[0])) &&
         write_section(file, root, "ENERGY", "Energy", energy_params,
                       sizeof(energy_params) / sizeof(energy_params[0])) &&
         write_section(file, root, "ERP_SERVICE", "Erp", erp_params,
                       sizeof(erp_params) / sizeof(erp_params[0]));

    if(fclose(file) != 0 || !ok || rename(CONFIG_TMP_FILE, CONFIG_FILE) != 0) {
        SAH_TRACEZ_ERROR(ME, "Cannot install %s: %s", CONFIG_FILE, strerror(errno));
        unlink(CONFIG_TMP_FILE);
        return false;
    }
    return true;
}

static void remove_config(void) {
    unlink(CONFIG_FILE);
    unlink(CONFIG_TMP_FILE);
}

static bool service_is_started(amxd_object_t* root) {
    const amxc_var_t* value = root == NULL ? NULL :
                              amxd_object_get_param_value(root, "StartService");
    return value != NULL && amxc_var_dyncast(bool, value);
}

static bool is_command_parameter(amxd_object_t* object, amxd_param_t* parameter) {
    static const char* const names[] = {
        "ERPServiceEnter", "ERPServiceExit",
        "EcoModeEnter", "EcoModeExit"
    };
    amxd_object_t* root = energy_root();
    const char* name = parameter == NULL ? NULL : amxd_param_get_name(parameter);

    if(root == NULL || object != root || name == NULL) {
        return false;
    }
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if(strcmp(name, names[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void store_last_erp_command(const char* command) {
    amxd_object_t* root = energy_root();
    amxd_param_t* parameter;
    const amxc_var_t* current_value;
    amxc_var_t previous_value;
    amxc_var_t old_values;
    amxc_var_t value;
    amxd_status_t status = amxd_status_unknown_error;

    if(root == NULL || command == NULL) {
        SAH_TRACEZ_WARNING(ME, "Failed to store the last ERP command");
        return;
    }

    parameter = amxd_object_get_param_def(root, "LastERPServiceCommand");
    current_value = amxd_object_get_param_value(root, "LastERPServiceCommand");
    amxc_var_init(&previous_value);
    amxc_var_init(&old_values);
    amxc_var_init(&value);
    if(parameter != NULL && current_value != NULL &&
       amxc_var_copy(&previous_value, current_value) == 0 &&
       amxc_var_set_type(&old_values, AMXC_VAR_ID_HTABLE) == 0 &&
       amxc_var_set_key(&old_values, "LastERPServiceCommand", &previous_value,
                        AMXC_VAR_FLAG_COPY) == 0 &&
       amxc_var_set(cstring_t, &value, command) == 0) {
        /* Update the runtime value internally while keeping the DM parameter
         * read-only to external callers. */
        status = amxc_var_copy(&parameter->value, &value) == 0 ?
                     amxd_status_ok : amxd_status_unknown_error;
        if(status == amxd_status_ok) {
            /* Notify amxrt so the persistent ODL store is updated. */
            amxd_object_trigger_changed(root, &old_values);
        }
    }
    amxc_var_clean(&previous_value);
    amxc_var_clean(&old_values);
    amxc_var_clean(&value);
    if(status != amxd_status_ok) {
        SAH_TRACEZ_WARNING(ME, "Failed to store the last ERP command (status %d)", status);
    }
}

/*
 * The Energy App worker is owned by the ERP init script. Keep the DM
 * operation best-effort so a missing ERP service does not break DM writes.
 */
static void worker_command_best_effort(const char* action) {
    char command[128];
    int result;

    if(action == NULL) {
        return;
    }
    (void) snprintf(command, sizeof(command), "%s %s >/dev/null 2>&1",
                    WORKER_INIT, action);
    result = system(command);
    if(result == -1 || !WIFEXITED(result) || WEXITSTATUS(result) != 0) {
        SAH_TRACEZ_WARNING(ME, "Worker command unavailable/failed: %s %s",
                           WORKER_INIT, action);
    }
}

ENERGY_EXPORT amxd_status_t _energyservice_sync(amxd_object_t* object,
                                 amxd_param_t* parameter,
                                 amxd_action_t reason,
                                 const amxc_var_t* const args,
                                 amxc_var_t* const retval,
                                 void* priv) {
    amxd_status_t status = amxd_action_param_write(object, parameter, reason, args, retval, priv);
    if(status != amxd_status_ok) {
        return status;
    }
    if(is_command_parameter(object, parameter)) {
        return amxd_status_ok;
    }
    if(!service_is_started(energy_root())) {
        remove_config();
        return amxd_status_ok;
    }
    return write_config(energy_root()) ? amxd_status_ok : amxd_status_unknown_error;
}

static amxd_status_t execute_erp_cli_parameter(amxd_object_t* object,
                                               amxd_param_t* parameter,
                                               amxd_action_t reason,
                                               const amxc_var_t* const args,
                                               amxc_var_t* const retval,
                                               void* priv,
                                               const char* expected_prefix) {
    const char* command;
    amxd_status_t status;
    erp_cli_result_t result;

    if(args == NULL || expected_prefix == NULL) {
        return amxd_status_invalid_value;
    }
    command = amxc_var_constcast(cstring_t, args);
    if(command == NULL) {
        return amxd_status_invalid_value;
    }
    result = send_erp_cli_command(command, expected_prefix);
    if(result == ERP_CLI_RESULT_INVALID) {
        return amxd_status_invalid_value;
    }
    if(result != ERP_CLI_RESULT_OK) {
        return amxd_status_unknown_error;
    }

    status = amxd_action_param_write(object, parameter, reason, args, retval, priv);
    if(status == amxd_status_ok) {
        store_last_erp_command(command);
    }
    return status;
}

ENERGY_EXPORT amxd_status_t _energyservice_erp_enter(amxd_object_t* object,
                                      amxd_param_t* parameter,
                                      amxd_action_t reason,
                                      const amxc_var_t* const args,
                                      amxc_var_t* const retval,
                                      void* priv) {
    return execute_erp_cli_parameter(object, parameter, reason, args, retval, priv,
                                      "erp cli enter");
}

ENERGY_EXPORT amxd_status_t _energyservice_erp_exit(amxd_object_t* object,
                                      amxd_param_t* parameter,
                                      amxd_action_t reason,
                                      const amxc_var_t* const args,
                                      amxc_var_t* const retval,
                                      void* priv) {
    return execute_erp_cli_parameter(object, parameter, reason, args, retval, priv,
                                      "erp cli exit");
}

ENERGY_EXPORT amxd_status_t _energyservice_status(amxd_object_t* object,
                                             amxd_function_t* function,
                                             amxc_var_t* args,
                                             amxc_var_t* retval) {
    erp_status_row_t rows[4] = {{0}};
    char output[ERP_STATUS_OUTPUT_LENGTH];

    (void) object;
    (void) function;
    (void) args;
    if(!refresh_erp_status(rows) || !format_erp_status(rows, output) || retval == NULL) {
        return amxd_status_unknown_error;
    }
    return amxc_var_set(cstring_t, retval, output) == 0 ?
           amxd_status_ok : amxd_status_unknown_error;
}

ENERGY_EXPORT amxd_status_t _energyservice_start_service(amxd_object_t* object,
                                          amxd_param_t* parameter,
                                          amxd_action_t reason,
                                          const amxc_var_t* const args,
                                          amxc_var_t* const retval,
                                          void* priv) {
    amxd_status_t status = amxd_action_param_write(object, parameter, reason, args, retval, priv);

    if(status != amxd_status_ok) {
        return status;
    }
    if(service_is_started(energy_root())) {
        if(!write_config(energy_root())) {
            return amxd_status_unknown_error;
        }
        worker_command_best_effort("start");
        return amxd_status_ok;
    } else {
        worker_command_best_effort("stop");
        remove_config();
        return amxd_status_ok;
    }
}

ENERGY_EXPORT int _energyservice_main(int reason, amxd_dm_t* dm, amxo_parser_t* parser) {
    (void) parser;
    energy_dm = dm;
    if(reason == AMXO_START) {
        if(service_is_started(energy_root())) {
            if(write_config(energy_root())) {
                worker_command_best_effort("start");
            }
        } else {
            remove_config();
        }
    } else if(reason == AMXO_STOP) {
        worker_command_best_effort("stop");
    }
    return 0;
}
