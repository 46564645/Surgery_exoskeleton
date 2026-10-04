/* L4: CLIP static command registration + ExoKit newline command convention.
 * Existing queries retain the S1 grammar. Only the two pulse commands accept
 * three bounded decimal arguments: current permit token, milli-effort, TTLms.
 */
#include "console_commands.h"
#include "board_console.h"
#include "board_runtime.h"
#include "s1_config.h"
#include "s2_config.h"
#include "can_monitor.h"
#include "s3_bench.h"
#include "s3_shoulder_service.h"
#include "shoulder_read_diagnostic.h"
#include "encoder_stream.h"
#include "clip.h"
#include "s3_console.h"
#include <stdio.h>
#include <string.h>

static char line[S1_LINE_MAX + 1U];
static size_t used;
static bool saw_cr, invalid_line, long_line;
static const char help_text[] = "OK commands=ping,status,help,?,canstatus,shoulder,elbow,elbowfeedback,shoulderfeedback,shoulderreadpos,shoulderreadvel,shoulderreadstatus,encstart,encstop,encfeedback,outputstatus,stopstatus,prepare,arm,cancel,ackfault,shoulderpulse,elbowpulse,s3help\r\n";
_Static_assert(sizeof(help_text) <= S1_REPLY_SIZE, "help must fit reserved reply slot");

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

static unsigned output_allowed(void)
{ S3BenchStatus s; S3Bench_GetStatus(&s); return (unsigned)s.output_allowed; }

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
            "can2_started=%u can2_baud=%lu can2_start_error=%u output_allowed=%u\r\n",
            (unsigned)a.started, (unsigned long)a.baudrate, (unsigned)a.start_error,
            (unsigned)b.started, (unsigned long)b.baudrate, (unsigned)b.start_error, output_allowed());
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
              "received_ms=na age_ms=na output_allowed=%u\r\n", (unsigned)S2_C620_ID, output_allowed());
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
        "received_ms=%lu age_ms=%lu output_allowed=%u\r\n",
        (unsigned)S2_C620_ID, (unsigned)s.valid, (unsigned)s.valid,
        (unsigned long)(pos_urad / 1000000U), (unsigned long)(pos_urad % 1000000U),
        vel_mrad < 0 ? "-" : "", (unsigned long)(magnitude / 1000U),
        (unsigned long)(magnitude % 1000U), (unsigned)s.rotor_angle_ticks,
        (int)s.rotor_speed_rpm, (int)s.torque_current_raw, (unsigned)s.temperature_c,
        (unsigned long)s.received_ms, (unsigned long)s.age_ms, output_allowed());
    if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
    else { reply("ERR format\r\n"); }
}

static void milli_text(float value, char out[16])
{
    /* Formatter bound only; never casts unbounded protocol floats to int. */
    if (value > 2147483.0f || value < -2147483.0f) {
        (void)snprintf(out, 16U, "range"); return;
    }
    int32_t scaled = (int32_t)(value * 1000.0f);
    (void)snprintf(out, 16U, "%ld", (long)scaled);
}
static void shoulder_feedback_reply(void)
{
    GdsFeedback s; bool wire;
    if (!CanMonitor_GetShoulderObservation(&s, &wire)) { reply("ERR feedback_unavailable\r\n"); return; }
    char out[S1_REPLY_SIZE], position[16] = "na", velocity[16] = "na";
    if (s.has_estimate) { milli_text(s.rotor_position_rad, position); milli_text(s.rotor_velocity_rad_s, velocity); }
    int n = snprintf(out, sizeof(out),
        "OK shoulder profile=v22 verified=0 est=%u hb=%u wire_valid=%u pos_mrad=%s vel_mrad_s=%s "
        "est_ms=%lu hb_ms=%lu state=%u error=%08lX fault=%u output_allowed=%u\r\n",
        (unsigned)s.has_estimate, (unsigned)s.has_heartbeat, (unsigned)wire, position, velocity,
        (unsigned long)s.estimate_ms, (unsigned long)s.heartbeat_ms, (unsigned)s.axis_state,
        (unsigned long)s.axis_error, (unsigned)s.fault_flags, output_allowed());
    if (n > 0 && (size_t)n < sizeof(out)) { reply(out); } else { reply("ERR format\r\n"); }
}
static void shoulder_read_reply(const char *command)
{
    if (strcmp(command, "shoulderreadstatus") != 0) {
        GdsReadField field = strcmp(command, "shoulderreadpos") == 0
            ? GDS_READ_POSITION : GDS_READ_VELOCITY;
        reply(ShoulderRead_Start(field, BoardRuntime_NowMs())
            ? "OK read_pending output_allowed=0\r\n" : "ERR read_blocked\r\n");
        return;
    }
    ShoulderReadStatus s; ShoulderRead_GetStatus(&s);
    static const char *const phases[] = {"idle", "pending", "received", "timeout", "tx_failed", "stream_owned", "s3_owned", "qualification_owned"};
    char out[S1_REPLY_SIZE], value[20] = "na", hex[17] = "-";
    BoardCanFrame frame = s.has_value ? s.matched : s.raw;
    if (s.has_value) {
        /* Keep six native decimal places without floating printf; bounded cast. */
        if (s.value > 2147.0f || s.value < -2147.0f) { (void)snprintf(value, sizeof(value), "range"); }
        else {
            int32_t scaled = (int32_t)(s.value * 1000000.0f);
            uint32_t magnitude = (uint32_t)(scaled < 0 ? -scaled : scaled);
            (void)snprintf(value, sizeof(value), "%s%lu.%06lu", scaled < 0 ? "-" : "",
                (unsigned long)(magnitude / 1000000U), (unsigned long)(magnitude % 1000000U));
        }
    }
    if (s.has_raw || s.has_value) {
        static const char digits[] = "0123456789ABCDEF";
        unsigned dlc = frame.dlc <= 8U ? frame.dlc : 8U;
        for (unsigned i = 0U; i < dlc; i++) {
            hex[2U * i] = digits[frame.data[i] >> 4]; hex[2U * i + 1U] = digits[frame.data[i] & 15U];
        }
        if (dlc != 0U) { hex[2U * dlc] = '\0'; }
    }
    unsigned fresh = s.has_value
        && (uint32_t)(BoardRuntime_NowMs() - s.received_ms) <= SHOULDER_READ_TIMEOUT_MS;
    int n = snprintf(out, sizeof(out),
        "OK src=encoder_sdo field=%s unit=%s phase=%s seq=%lu ep=%u accepted=%u value=%s fresh=%u "
        "sent_ms=%lu rx_ms=%lu id=%03X dlc=%u raw=%s rejected=%lu output_allowed=%u\r\n",
        s.field == GDS_READ_POSITION ? "pos" : "vel", s.field == GDS_READ_POSITION ? "rev" : "rev_s",
        phases[(unsigned)s.phase], (unsigned long)s.sequence, (unsigned)GdsSdoRead_Endpoint(s.field),
        (unsigned)s.accepted, value, fresh, (unsigned long)s.sent_ms,
        (unsigned long)frame.received_ms, (unsigned)frame.id, (unsigned)frame.dlc, hex,
        (unsigned long)s.rejected, output_allowed());
    if (n > 0 && (size_t)n < sizeof(out)) { reply(out); } else { reply("ERR format\r\n"); }
}
static void encoder_value_text(float value, char out[20])
{
    if (value > 2147.0f || value < -2147.0f) { (void)snprintf(out, 20U, "range"); return; }
    int32_t scaled = (int32_t)(value * 1000000.0f);
    uint32_t magnitude = (uint32_t)(scaled < 0 ? -scaled : scaled);
    (void)snprintf(out, 20U, "%s%lu.%06lu", scaled < 0 ? "-" : "",
        (unsigned long)(magnitude / 1000000U), (unsigned long)(magnitude % 1000000U));
}
static void encoder_stream_reply(const char *command)
{
    uint32_t now_ms = BoardRuntime_NowMs();
    if (strcmp(command, "encstart") == 0) {
        reply(EncoderStream_Start(now_ms) ? "OK encoder_stream_started output_allowed=0\r\n"
              : "ERR encoder_stream_blocked\r\n"); return;
    }
    if (strcmp(command, "encstop") == 0) {
        EncoderStream_Stop(now_ms); reply("OK encoder_stream_stopped output_allowed=0\r\n"); return;
    }
    EncoderStreamStatus s; EncoderStream_GetStatus(now_ms, &s);
    char out[S1_REPLY_SIZE], position[20] = "na", velocity[20] = "na";
    if (s.has_pair) { encoder_value_text(s.position_rev, position); encoder_value_text(s.velocity_rev_s, velocity); }
    int n = snprintf(out, sizeof(out),
        "OK src=encoder_sdo active=%u reason=%u valid=%u pairs=%lu pos_rev=%s vel_rev_s=%s "
        "pos_ms=%lu vel_ms=%lu age_ms=%lu elapsed_ms=%lu tx=%lu rejected=%lu output_allowed=%u\r\n",
        (unsigned)s.active, (unsigned)s.reason, (unsigned)s.valid, (unsigned long)s.pairs,
        position, velocity, (unsigned long)s.position_ms, (unsigned long)s.velocity_ms,
        (unsigned long)s.age_ms, (unsigned long)s.elapsed_ms, (unsigned long)s.requests,
        (unsigned long)s.rejected, output_allowed());
    if (n > 0 && (size_t)n < sizeof(out)) { reply(out); } else { reply("ERR format\r\n"); }
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
            "OK stage=S2_CAN_RX uptime_ms=%lu output_allowed=%u uart_errors=%lu "
            "rx_overflows=%lu tx_timeouts=%lu scheduler_overruns=%lu\r\n",
            (unsigned long)ctx->now_ms, output_allowed(), (unsigned long)stats.uart_errors,
            (unsigned long)stats.rx_overflows, (unsigned long)stats.tx_timeouts,
            (unsigned long)ctx->overruns);
        if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
        else { reply("ERR format\r\n"); }
    } else if (strcmp(cmd->name, "canstatus") == 0 || strcmp(cmd->name, "shoulder") == 0
               || strcmp(cmd->name, "elbow") == 0) {
        can_reply(cmd->name);
    } else if (strcmp(cmd->name, "outputstatus") == 0) {
        S3BenchStatus s;
        char out[S1_REPLY_SIZE];
        S3Bench_GetStatus(&s);
        int n = snprintf(out, sizeof(out),
            "OK stage=S2_CAN_RX s3_state=%u reason=%u configured=%u transport_bound=%u "
            "output_allowed=%u request_valid=%u stop_attempted=%u prep_state=%u arm_token=%lu feedback_pending=%u reader=%u reader_reason=%u\r\n",
            (unsigned)s.state, (unsigned)s.reason, (unsigned)s.configured,
            (unsigned)s.transport_bound, (unsigned)s.output_allowed,
            (unsigned)s.request_valid, (unsigned)s.stop_attempted,
            (unsigned)s.preparation, (unsigned long)s.arm_token,
            (unsigned)s.feedback_pending, s.reader_state, s.reader_reason);
        if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
        else { reply("ERR format\r\n"); }
    } else if (strcmp(cmd->name, "stopstatus") == 0) {
        S3BenchStatus gate; S3Bench_GetStatus(&gate);
        if (gate.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH) {
            uint32_t now = BoardRuntime_NowMs();
            ShoulderStandstillStatus s = S3ShoulderService_GetStandstill(now);
            S3ReaderStatus reader = S3ShoulderService_Get(); char out[S1_REPLY_SIZE];
            int n = snprintf(out, sizeof(out),
                "OK policy=%u source=shoulder_sdo seen=%u fresh=%u stationary=%u dwell_ms=%lu age_ms=%lu "
                "observed=%u reader=%u stop_accepted=%u purge_ok=%u stop_delivery_failed=%u\r\n",
                (unsigned)gate.stop_policy, (unsigned)s.seen, (unsigned)s.fresh, (unsigned)s.stationary,
                (unsigned long)s.dwell_ms, (unsigned long)s.age_ms, (unsigned)reader.stop_observed,
                (unsigned)reader.state, (unsigned)gate.stop_accepted, (unsigned)gate.purge_ok,
                (unsigned)gate.stop_delivery_failed);
            if (n > 0 && (size_t)n < sizeof(out)) { reply(out); } else { reply("ERR format\r\n"); }
            return;
        }
        C620StandstillStatus s = CanMonitor_GetElbowStandstill(BoardRuntime_NowMs());
        char out[S1_REPLY_SIZE];
        int n = snprintf(out, sizeof(out),
            "OK policy=%u source=c620_feedback seen=%u fresh=%u stationary=%u dwell_ms=%lu age_ms=%lu "
            "stop_attempted=%u stop_accepted=%u purge_ok=%u stop_delivery_failed=%u\r\n",
            (unsigned)gate.stop_policy, (unsigned)s.seen, (unsigned)s.fresh, (unsigned)s.stationary,
            (unsigned long)s.dwell_ms, (unsigned long)s.age_ms, (unsigned)gate.stop_attempted,
            (unsigned)gate.stop_accepted, (unsigned)gate.purge_ok, (unsigned)gate.stop_delivery_failed);
        if (n > 0 && (size_t)n < sizeof(out)) { reply(out); } else { reply("ERR format\r\n"); }
    } else if (strcmp(cmd->name, "prepare") == 0) {
        const CommandContext *ctx = context;
        reply(S3Bench_Prepare(ctx->now_ms) ? "OK preparing output_allowed=0\r\n" : "ERR blocked\r\n");
    } else if (strcmp(cmd->name, "arm") == 0) {
        const CommandContext *ctx = context;
        if (!S3Bench_Arm(ctx->now_ms)) { reply("ERR blocked\r\n"); }
        else {
            S3BenchStatus s; char out[64]; S3Bench_GetStatus(&s);
            (void)snprintf(out, sizeof(out), "OK armed token=%lu\r\n", (unsigned long)s.arm_token);
            reply(out);
        }
    } else if (strcmp(cmd->name, "cancel") == 0) {
        S3Bench_Cancel(); reply("OK cancelled output_allowed=0\r\n");
    } else if (strcmp(cmd->name, "ackfault") == 0) {
        const CommandContext *ctx = context;
        reply(S3Bench_AcknowledgeFault(ctx->now_ms) ? "OK fault_acknowledged output_allowed=0\r\n" : "ERR blocked\r\n");
    } else if (strcmp(cmd->name, "shoulderfeedback") == 0) {
        shoulder_feedback_reply();
    } else if (strcmp(cmd->name, "shoulderreadpos") == 0 || strcmp(cmd->name, "shoulderreadvel") == 0
               || strcmp(cmd->name, "shoulderreadstatus") == 0) {
        shoulder_read_reply(cmd->name);
    } else if (strcmp(cmd->name, "encstart") == 0 || strcmp(cmd->name, "encstop") == 0
               || strcmp(cmd->name, "encfeedback") == 0) {
        encoder_stream_reply(cmd->name);
    } else if (strcmp(cmd->name, "elbowfeedback") == 0) {
        elbow_feedback_reply();
    } else if (!S3Console_Command(cmd->name, ((const CommandContext *)context)->now_ms)) {
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
static CLIP_DEF_ROOT_COMMAND(shoulderfeedback_cmd, "shoulderfeedback", "unverified SIMPLE wire observation", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(outputstatus_cmd, "outputstatus", "locked output gate status", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(shoulderreadpos_cmd, "shoulderreadpos", "one bounded encoder position read per boot", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(shoulderreadvel_cmd, "shoulderreadvel", "one bounded encoder velocity read per boot", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(shoulderreadstatus_cmd, "shoulderreadstatus", "cached SDO response and source", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(encstart_cmd, "encstart", "start one bounded IDLE encoder observation session", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(encstop_cmd, "encstop", "stop encoder observation and invalidate cached pair", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(encfeedback_cmd, "encfeedback", "native encoder pair freshness and bounded session", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(stopstatus_cmd, "stopstatus", "receive standstill and stop submission evidence", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(prepare_cmd, "prepare", "verified zero/mode/closed-loop preparation", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(arm_cmd, "arm", "fresh manual software permit", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(cancel_cmd, "cancel", "withdraw and stop both joints", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(ackfault_cmd, "ackfault", "acknowledge only with selected stop-policy evidence", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3feedback_cmd, "s3feedback", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3properties_cmd, "s3properties", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3reader_cmd, "s3reader", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3anchor_cmd, "s3anchor", "S3 relative origin history, read only", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3limits_cmd, "s3limits", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3timing_cmd, "s3timing", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3stopcfg_cmd, "s3stopcfg", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(s3help_cmd, "s3help", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(qualify_cmd, "qualify", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(qualstatus_cmd, "qualstatus", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(qualerror_cmd, "qualerror", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(qualcancel_cmd, "qualcancel", "S3 cached query or bounded qualification", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT(commands, NULL, command_event)
    CLIP_DEF_ADD_ROOT_COMMAND(&ping_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&status_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&canstatus_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&shoulder_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&elbow_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&elbowfeedback_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&shoulderfeedback_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&shoulderreadpos_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&shoulderreadvel_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&shoulderreadstatus_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&encstart_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&encstop_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&encfeedback_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&outputstatus_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&stopstatus_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&prepare_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&arm_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&cancel_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&ackfault_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3feedback_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3properties_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3reader_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3anchor_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3limits_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3timing_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3stopcfg_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&s3help_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&qualify_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&qualstatus_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&qualerror_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&qualcancel_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&help_cmd)
CLIP_DEF_ROOT_END()

static bool unsigned_arg(char **cursor, uint32_t maximum, uint32_t *value)
{
    char *p = *cursor; while (*p == ' ') { p++; }
    if (*p < '0' || *p > '9') { return false; }
    uint32_t n = 0U;
    do {
        uint32_t digit = (uint32_t)(*p - '0');
        if (digit > maximum || n > (maximum - digit) / 10U) { return false; }
        n = n * 10U + digit; p++;
    } while (*p >= '0' && *p <= '9');
    if (*p != ' ' && *p != '\0') { return false; }
    *cursor = p; *value = n; return true;
}
static bool signed_arg(char **cursor, int32_t *value)
{
    char *p = *cursor; while (*p == ' ') { p++; }
    bool negative = *p == '-'; if (negative) { p++; }
    if (*p < '0' || *p > '9') { return false; }
    uint32_t n;
    if (!unsigned_arg(&p, INT32_MAX, &n)) { return false; }
    *value = negative ? -(int32_t)n : (int32_t)n; *cursor = p; return true;
}
static void pulse_reply(unsigned joint, char *args, uint32_t now_ms)
{
    uint32_t token, ttl; int32_t effort;
    if (!unsigned_arg(&args, UINT32_MAX, &token) || !signed_arg(&args, &effort)
        || !unsigned_arg(&args, INT32_MAX, &ttl)) {
        S3Bench_RejectRequest(); reply("ERR args\r\n"); return;
    }
    while (*args == ' ') { args++; }
    if (*args != '\0') { S3Bench_RejectRequest(); reply("ERR args\r\n"); return; }
    reply(S3Bench_Request(token, joint, (float)effort / 1000.0f, ttl, now_ms)
        ? "OK request\r\n" : "ERR blocked\r\n");
}

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
              || (*end >= '0' && *end <= '9' && end != token) || *end == '?')) { reply("ERR syntax\r\n"); return true; }
        end++;
    }
    char *rest = end;
    while (*rest == ' ') { rest++; }
    if ((size_t)(end - token) == strlen("qualfield") && strncmp(token, "qualfield", (size_t)(end - token)) == 0) {
        uint32_t field;
        bool valid = unsigned_arg(&rest, GDS_PROPERTY_COUNT - 1U, &field);
        while (*rest == ' ') { rest++; }
        if (!valid || *rest != '\0') { reply("ERR args\r\n"); }
        else if (!BoardConsole_HasFault()) { S3Console_QualificationField((unsigned)field); }
        return true;
    }
    bool shoulder_pulse = (size_t)(end - token) == strlen("shoulderpulse")
        && strncmp(token, "shoulderpulse", (size_t)(end - token)) == 0;
    bool elbow_pulse = (size_t)(end - token) == strlen("elbowpulse")
        && strncmp(token, "elbowpulse", (size_t)(end - token)) == 0;
    if (shoulder_pulse || elbow_pulse) {
        if (!BoardConsole_HasFault()) { pulse_reply(shoulder_pulse ? 0U : 1U, rest, context->now_ms); }
        return true;
    }
    /* Existing queries remain strict: '? extra' and 'ping ""' are rejected. */
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
