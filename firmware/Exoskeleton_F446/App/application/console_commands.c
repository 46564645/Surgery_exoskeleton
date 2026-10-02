/* L4: CLIP static command registration + ExoKit newline command convention.
 * This S1 grammar intentionally accepts one unquoted token and no arguments.
 */
#include "console_commands.h"
#include "board_console.h"
#include "board_runtime.h"
#include "s1_config.h"
#include "s2_config.h"
#include "can_monitor.h"
#include "clip.h"
#include <stdio.h>
#include <string.h>

static char line[S1_LINE_MAX + 1U];
static size_t used;
static bool saw_cr, invalid_line, long_line;
static const char help_text[] = "OK commands=ping,status,help,?,canstatus,shoulder,elbow,elbowfeedback\r\n";

typedef struct {
    uint32_t now_ms;
    uint32_t overruns;
} CommandContext;

static void reply(const char *text)
{
    /* Poll reserved a whole response slot. Only this main-loop producer writes.
     * A false result can still occur if an IRQ has latched a transport fault.
     */
    (void)BoardConsole_Write((const uint8_t *)text, strlen(text));
}

static void can_reply(const char *command)
{
    char out[S1_REPLY_SIZE];
    int n;
    if (strcmp(command, "canstatus") == 0) {
        BoardCanStats a, b;
        (void)BoardCan_GetStats(BOARD_CAN_SHOULDER, &a);
        (void)BoardCan_GetStats(BOARD_CAN_ELBOW, &b);
        n = snprintf(out, sizeof(out),
            "OK can1_started=%u can1_baud=%lu can1_start_error=%u "
            "can2_started=%u can2_baud=%lu can2_start_error=%u output_allowed=0\r\n",
            (unsigned)a.started, (unsigned long)a.baudrate, (unsigned)a.start_error,
            (unsigned)b.started, (unsigned long)b.baudrate, (unsigned)b.start_error);
    } else {
        BoardCanBus bus = strcmp(command, "shoulder") == 0 ? BOARD_CAN_SHOULDER : BOARD_CAN_ELBOW;
        BoardCanStats s;
        BoardCanFrame frame = {0};
        (void)BoardCan_GetStats(bus, &s);
        bool seen = CanMonitor_GetLast(bus, &frame);
        char hex[17] = "-", age[11] = "na", id[5] = "na";
        static const char digits[] = "0123456789ABCDEF";
        if (seen) {
            /* An RX IRQ can timestamp a frame after App_Poll sampled its tick.
             * Read time after the frame snapshot, not the scheduling timestamp. */
            uint32_t now_ms = BoardRuntime_NowMs();
            (void)snprintf(age, sizeof(age), "%lu", (unsigned long)(uint32_t)(now_ms - frame.received_ms));
            (void)snprintf(id, sizeof(id), "%03X", (unsigned)frame.id);
            for (unsigned i = 0U; i < frame.dlc; i++) {
                hex[2U * i] = digits[frame.data[i] >> 4];
                hex[2U * i + 1U] = digits[frame.data[i] & 15U];
            }
            if (frame.dlc != 0U) { hex[2U * frame.dlc] = '\0'; }
        }
        n = snprintf(out, sizeof(out),
            "OK bus=%s started=%u start_error=%u rx=%lu drop=%lu invalid=%lu "
            "fov=%lu err=%lu last_err=%08lX bus_off=%u queued=%u seen=%u id=%s dlc=%u data=%s age_ms=%s\r\n",
            command, (unsigned)s.started, (unsigned)s.start_error,
            (unsigned long)s.received, (unsigned long)s.dropped, (unsigned long)s.invalid,
            (unsigned long)s.fifo_overruns, (unsigned long)s.errors, (unsigned long)s.last_error,
            (unsigned)s.bus_off, (unsigned)s.queued, (unsigned)seen, id,
            (unsigned)frame.dlc, hex, age);
    }
    if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
    else { reply("ERR format\r\n"); }
}

static void elbow_feedback_reply(void)
{
    C620Feedback s;
    if (!CanMonitor_GetElbowFeedback(&s)) { reply("ERR feedback_unavailable\r\n"); return; }
    char out[S1_REPLY_SIZE];
    if (!s.has_sample) {
        int n = snprintf(out, sizeof(out),
              "OK bus=elbow motor_id=%u seen=0 valid=0 online=0 rotor_pos_rad=na "
              "rotor_vel_rad_s=na angle_ticks=na rpm=na current_raw=na temp_c=na "
              "received_ms=na age_ms=na output_allowed=0\r\n", (unsigned)S2_C620_ID);
        if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
        else { reply("ERR format\r\n"); }
        return;
    }
    /* Fixed-point text avoids enabling newlib-nano floating printf. Internal
     * values remain rotor SI units; no gear ratio or joint-zero assumption. */
    uint32_t pos_urad = (uint32_t)(s.rotor_position_rad * 1000000.0f + 0.5f);
    int32_t vel_mrad = (int32_t)(s.rotor_velocity_rad_s * 1000.0f
                               + (s.rotor_velocity_rad_s < 0.0f ? -0.5f : 0.5f));
    uint32_t magnitude = (uint32_t)(vel_mrad < 0 ? -vel_mrad : vel_mrad);
    int n = snprintf(out, sizeof(out),
        "OK bus=elbow motor_id=%u seen=1 valid=%u online=%u rotor_pos_rad=%lu.%06lu "
        "rotor_vel_rad_s=%s%lu.%03lu angle_ticks=%u rpm=%d current_raw=%d temp_c=%u "
        "received_ms=%lu age_ms=%lu output_allowed=0\r\n",
        (unsigned)S2_C620_ID, (unsigned)s.valid, (unsigned)s.valid,
        (unsigned long)(pos_urad / 1000000U), (unsigned long)(pos_urad % 1000000U),
        vel_mrad < 0 ? "-" : "", (unsigned long)(magnitude / 1000U),
        (unsigned long)(magnitude % 1000U), (unsigned)s.rotor_angle_ticks,
        (int)s.rotor_speed_rpm, (int)s.torque_current_raw, (unsigned)s.temperature_c,
        (unsigned long)s.received_ms, (unsigned long)s.age_ms);
    if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
    else { reply("ERR format\r\n"); }
}

static void command_reply(const struct clip *self, const struct clip_command *cmd,
                          size_t argc, struct clip_arg_value argv[], void *context)
{
    (void)self;
    (void)argv;
    if (argc != 0U) { reply("ERR args\r\n"); return; }
    if (strcmp(cmd->name, "ping") == 0) {
        reply("OK pong\r\n");
    } else if (strcmp(cmd->name, "status") == 0) {
        const CommandContext *ctx = context;
        BoardConsoleStats stats;
        char out[S1_REPLY_SIZE];
        BoardConsole_GetStats(&stats);
        int n = snprintf(out, sizeof(out),
            "OK stage=S2_CAN_RX uptime_ms=%lu output_allowed=0 uart_errors=%lu "
            "rx_overflows=%lu tx_timeouts=%lu scheduler_overruns=%lu\r\n",
            (unsigned long)ctx->now_ms, (unsigned long)stats.uart_errors,
            (unsigned long)stats.rx_overflows, (unsigned long)stats.tx_timeouts,
            (unsigned long)ctx->overruns);
        if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
        else { reply("ERR format\r\n"); }
    } else if (strcmp(cmd->name, "canstatus") == 0 || strcmp(cmd->name, "shoulder") == 0
               || strcmp(cmd->name, "elbow") == 0) {
        can_reply(cmd->name);
    } else if (strcmp(cmd->name, "elbowfeedback") == 0) {
        elbow_feedback_reply();
    } else {
        reply(help_text);
    }
}

static void command_event(const struct clip *self, clip_event_t event,
                          union clip_event_arg *arg, void *context)
{
    (void)self; (void)arg; (void)context;
    if (event == CLIP_EVENT_HELP) {
        reply(help_text);
    } else if (event == CLIP_EVENT_COMMAND_NOT_FOUND) {
        reply("ERR unknown\r\n");
    } else if (event == CLIP_EVENT_ARGUMENTS_ERROR) {
        reply("ERR args\r\n");
    }
}

static CLIP_DEF_ROOT_COMMAND(ping_cmd, "ping", "link check", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(status_cmd, "status", "system status", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(canstatus_cmd, "canstatus", "CAN startup", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(shoulder_cmd, "shoulder", "CAN1 raw observation", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(elbow_cmd, "elbow", "CAN2 raw observation", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(help_cmd, "help", "command list", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(elbowfeedback_cmd, "elbowfeedback", "C620 rotor freshness", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT(commands, NULL, command_event)
    CLIP_DEF_ADD_ROOT_COMMAND(&ping_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&status_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&canstatus_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&shoulder_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&elbow_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&elbowfeedback_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&help_cmd)
CLIP_DEF_ROOT_END()

static bool dispatch_line(CommandContext *context)
{
    if (long_line) { reply("ERR line_too_long\r\n"); return true; }
    if (invalid_line) { reply("ERR syntax\r\n"); return true; }
    line[used] = '\0';
    char *token = line;
    while (*token == ' ') { token++; }
    if (*token == '\0') { return false; }
    char *end = token;
    while (*end != '\0' && *end != ' ') {
        if (!((*end >= 'a' && *end <= 'z') || (*end >= 'A' && *end <= 'Z')
              || *end == '?')) { reply("ERR syntax\r\n"); return true; }
        end++;
    }
    char *rest = end;
    while (*rest == ' ') { rest++; }
    /* Reject '? extra' and 'ping ""' before CLIP's help/argument shortcuts. */
    if (*rest != '\0') { reply("ERR args\r\n"); return true; }
    *end = '\0';
    if (!BoardConsole_HasFault()) {
        clip_cmd_parse_line(&commands, NULL, token, context);
    }
    return true;
}

void ConsoleCommands_Init(void)
{
    used = 0U;
    saw_cr = false;
    invalid_line = false;
    long_line = false;
}

void ConsoleCommands_Poll(uint32_t now_ms, uint32_t scheduler_overruns)
{
    CommandContext context = {now_ms, scheduler_overruns};
    if (BoardConsole_TxFree() < S1_REPLY_SIZE || BoardConsole_HasFault()) { return; }
    for (size_t i = 0U; i < S1_RX_BUDGET; i++) {
        uint8_t ch;
        if (BoardConsole_Read(&ch, 1U) != 1U) { break; }
        if (ch == '\n') {
            bool handled = dispatch_line(&context);
            ConsoleCommands_Init();
            if (handled) { break; }
        } else if (!invalid_line && !long_line) {
            if (saw_cr) { invalid_line = true; }
            else if (ch == '\r') { saw_cr = true; }
            else if (ch < 0x20U || ch > 0x7EU) { invalid_line = true; }
            else if (used == S1_LINE_MAX) { long_line = true; }
            else { line[used++] = (char)ch; }
        }
    }
}
