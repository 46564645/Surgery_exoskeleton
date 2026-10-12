/* Temporary R2 elbow evidence recorder. This file adds no CAN commands.
 * All wrapped application calls are main-loop calls; the real call runs once. */
#include "board_console.h"
#include "main.h"
#include "c620_feedback.h"
#include "elbow_session_angle.h"
#include "output_gateway.h"
#include "recorder.h"
#include "s1_config.h"
#include "s2_config.h"
#include "clip.h"
#include <stdio.h>
#include <string.h>

enum { EC_RX = 1, EC_SESSION_FAULT = 2, EC_GATE_FAULT = 3 };

typedef struct {
    uint32_t last_ms, last_seq, seq, rx_ms, now_ms, timeout, limit_bits;
    uint16_t last_ticks, ticks;
    int16_t rpm;
    int8_t direction;
    uint8_t pre, post;
    bool present, has, valid, stationary;
} EcSessionFault;

typedef struct {
    uint32_t now_ms;
    uint8_t pre, post, reason, safety;
    bool present, inputs_present, transport, joint0_valid, joint1_valid;
    bool drivers, stop_clear, stationary;
} EcGateFault;

static BbRecorder recorder;
static EcSessionFault session_fault;
static EcGateFault gate_fault;
static uint16_t cursor;
static uint8_t trigger_kind;
_Static_assert(sizeof(float) == sizeof(uint32_t), "Capture needs 32-bit float bits");
_Static_assert(sizeof(recorder) + sizeof(session_fault) + sizeof(gate_fault) < 6144U,
               "Elbow capture RAM budget");

bool __real_C620_Receive(C620Device *device, BoardCanBus bus,
                        const BoardCanFrame *frame, uint32_t now_ms);
void __real_ElbowSessionAngle_Observe(ElbowSessionAngle *session,
                                     const C620Feedback *sample, bool stationary,
                                     uint32_t now_ms, uint32_t timeout_ms,
                                     float max_speed_rad_s, int8_t direction);
void __real_OutputGateway_Step(OutputGateway *gateway, const SafetyInputs *inputs,
                               uint32_t now_ms);
void __real_clip_cmd_parse_line(const struct clip *self, const struct clip_command *cmd,
                                char *line, void *context);

static uint32_t lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}

static void record(const BbEvent *event, bool trigger)
{
    uint32_t saved = lock();
    bool first = trigger && !recorder.triggered && !recorder.frozen;
    Bb_Push(&recorder, event, trigger);
    if (first) {
        trigger_kind = event->kind;
        /* Stop on the actual decision, even if power is removed before
         * the old recorder's 16 post-trigger slots can be filled. */
        Bb_Freeze(&recorder);
    }
    __set_PRIMASK(saved);
}

bool __wrap_C620_Receive(C620Device *device, BoardCanBus bus,
                         const BoardCanFrame *frame, uint32_t now_ms)
{
    bool accepted = __real_C620_Receive(device, bus, frame, now_ms);
    if (frame != NULL && bus == BOARD_CAN_ELBOW
        && frame->id == (uint16_t)(0x200U + S2_C620_ID)) {
        BbEvent event = {0};
        event.kind = EC_RX;
        event.ms = now_ms; /* Main-loop processing time, not wire time. */
        event.aux = frame->received_ms;
        event.esr = accepted && device != NULL ? device->feedback.sequence : 0U;
        event.id = frame->id;
        event.dlc = frame->dlc;
        event.flags = (uint8_t)bus;
        event.status = (uint8_t)accepted;
        memcpy(event.data, frame->data, sizeof(event.data));
        record(&event, false);
    }
    return accepted;
}

void __wrap_ElbowSessionAngle_Observe(ElbowSessionAngle *session,
                                      const C620Feedback *sample, bool stationary,
                                      uint32_t now_ms, uint32_t timeout_ms,
                                      float max_speed_rad_s, int8_t direction)
{
    ElbowSessionPhase before = session == NULL ? ELBOW_SESSION_UNSET : session->phase;
    uint16_t last_ticks = session == NULL ? 0U : session->last_ticks;
    uint32_t last_ms = session == NULL ? 0U : session->last_ms;
    uint32_t last_seq = session == NULL ? 0U : session->last_sequence;
    C620Feedback observed = sample == NULL ? (C620Feedback){0} : *sample;
    uint32_t limit_bits;
    memcpy(&limit_bits, &max_speed_rad_s, sizeof(limit_bits));

    __real_ElbowSessionAngle_Observe(session, sample, stationary, now_ms, timeout_ms,
                                     max_speed_rad_s, direction);
    if (session == NULL || before == ELBOW_SESSION_FAULT
        || session->phase != ELBOW_SESSION_FAULT) { return; }

    uint32_t saved = lock();
    if (!session_fault.present) {
        session_fault = (EcSessionFault){
            .last_ms = last_ms, .last_seq = last_seq,
            .seq = observed.sequence, .rx_ms = observed.received_ms,
            .now_ms = now_ms, .timeout = timeout_ms, .limit_bits = limit_bits,
            .last_ticks = last_ticks, .ticks = observed.rotor_angle_ticks,
            .rpm = observed.rotor_speed_rpm, .direction = direction,
            .pre = (uint8_t)before, .post = (uint8_t)session->phase,
            .present = true, .has = sample != NULL && observed.has_sample,
            .valid = sample != NULL && observed.valid, .stationary = stationary
        };
    }
    __set_PRIMASK(saved);

    BbEvent event = {0};
    event.kind = EC_SESSION_FAULT;
    event.ms = now_ms;
    event.aux = observed.received_ms;
    event.esr = observed.sequence;
    event.status = 1U;
    record(&event, true);
}

void __wrap_OutputGateway_Step(OutputGateway *gateway, const SafetyInputs *inputs,
                               uint32_t now_ms)
{
    OutputState before = gateway == NULL ? OUTPUT_SAFE_OFF : gateway->state;
    __real_OutputGateway_Step(gateway, inputs, now_ms);
    if (gateway == NULL || before == OUTPUT_FAULT || gateway->state != OUTPUT_FAULT) {
        return;
    }

    uint32_t saved = lock();
    if (!gate_fault.present) {
        gate_fault = (EcGateFault){
            .now_ms = now_ms, .pre = (uint8_t)before, .post = (uint8_t)gateway->state,
            .reason = (uint8_t)gateway->reason, .safety = (uint8_t)gateway->safety_reason,
            .present = true, .inputs_present = inputs != NULL,
            .transport = inputs != NULL && inputs->transport_healthy,
            .joint0_valid = inputs != NULL && inputs->joint[0].valid,
            .joint1_valid = inputs != NULL && inputs->joint[1].valid,
            .drivers = inputs != NULL && inputs->drivers_ready,
            .stop_clear = inputs != NULL && inputs->operator_stop_clear,
            .stationary = inputs != NULL && inputs->feedback_stationary
        };
    }
    __set_PRIMASK(saved);

    BbEvent event = {0};
    event.kind = EC_GATE_FAULT;
    event.ms = now_ms;
    event.status = 1U;
    record(&event, true);
}

static bool write_reply(const char *text, int length)
{
    return length > 0 && (size_t)length < S1_REPLY_SIZE
        && BoardConsole_Write((const uint8_t *)text, (size_t)length);
}

static bool command(const char *line)
{
    char out[S1_REPLY_SIZE];
    int n = 0;
    if (strcmp(line, "ecstatus") == 0) {
        uint32_t saved = lock();
        unsigned count = recorder.count, frozen = recorder.frozen, triggered = recorder.triggered;
        unsigned kind = trigger_kind, next = cursor;
        uint32_t seq = recorder.trigger_seq, overwritten = recorder.overwritten;
        uint32_t suppressed = recorder.suppressed;
        __set_PRIMASK(saved);
        n = snprintf(out, sizeof(out),
            "OK ec v=1 bus=2 count=%u frozen=%u triggered=%u trigger_seq=%lu "
            "trigger_kind=%u overwritten=%lu suppressed=%lu cursor=%u\r\n",
            count, frozen, triggered, (unsigned long)seq, kind,
            (unsigned long)overwritten, (unsigned long)suppressed, next);
    } else if (strcmp(line, "ecfault") == 0) {
        uint32_t saved = lock();
        EcSessionFault f = session_fault;
        __set_PRIMASK(saved);
        if (!f.present) { n = snprintf(out, sizeof(out), "OK ec fault=0\r\n"); }
        else {
            n = snprintf(out, sizeof(out),
                "OK ec fault=1 pre=%u post=%u last_ticks=%u last_ms=%lu last_seq=%lu "
                "has=%u valid=%u ticks=%u rpm=%d seq=%lu rx_ms=%lu now_ms=%lu "
                "timeout=%lu limit_bits=%08lX dir=%d still=%u\r\n",
                (unsigned)f.pre, (unsigned)f.post, (unsigned)f.last_ticks,
                (unsigned long)f.last_ms, (unsigned long)f.last_seq,
                (unsigned)f.has, (unsigned)f.valid, (unsigned)f.ticks, (int)f.rpm,
                (unsigned long)f.seq, (unsigned long)f.rx_ms,
                (unsigned long)f.now_ms, (unsigned long)f.timeout,
                (unsigned long)f.limit_bits, (int)f.direction, (unsigned)f.stationary);
        }
    } else if (strcmp(line, "ecgate") == 0) {
        uint32_t saved = lock();
        EcGateFault f = gate_fault;
        __set_PRIMASK(saved);
        if (!f.present) { n = snprintf(out, sizeof(out), "OK ec gate=0\r\n"); }
        else {
            n = snprintf(out, sizeof(out),
                "OK ec gate=1 pre=%u post=%u reason=%u safety=%u now_ms=%lu "
                "in=%u transport=%u j0=%u j1=%u drivers=%u stop_clear=%u still=%u\r\n",
                (unsigned)f.pre, (unsigned)f.post, (unsigned)f.reason,
                (unsigned)f.safety, (unsigned long)f.now_ms,
                (unsigned)f.inputs_present, (unsigned)f.transport,
                (unsigned)f.joint0_valid, (unsigned)f.joint1_valid,
                (unsigned)f.drivers, (unsigned)f.stop_clear,
                (unsigned)f.stationary);
        }
    } else if (strcmp(line, "ecfreeze") == 0) {
        uint32_t saved = lock();
        Bb_Freeze(&recorder);
        __set_PRIMASK(saved);
        n = snprintf(out, sizeof(out), "OK ec frozen=1\r\n");
    } else if (strcmp(line, "ecrewind") == 0) {
        cursor = 0U;
        n = snprintf(out, sizeof(out), "OK ec cursor=0\r\n");
    } else if (strcmp(line, "ecnext") == 0) {
        BbEvent event;
        uint32_t saved = lock();
        bool frozen = recorder.frozen;
        bool available = Bb_Read(&recorder, cursor, &event);
        __set_PRIMASK(saved);
        if (!frozen) { n = snprintf(out, sizeof(out), "ERR ec freeze_first\r\n"); }
        else if (!available) { n = snprintf(out, sizeof(out), "OK ec end=1\r\n"); }
        else {
            char hex[17];
            static const char digits[] = "0123456789ABCDEF";
            for (unsigned i = 0U; i < 8U; ++i) {
                hex[2U * i] = digits[event.data[i] >> 4];
                hex[2U * i + 1U] = digits[event.data[i] & 15U];
            }
            hex[16] = '\0';
            n = snprintf(out, sizeof(out),
                "OK ec i=%u seq=%lu ms=%lu kind=%u rx_ms=%lu fb_seq=%lu "
                "id=%08lX dlc=%u status=%u data=%s\r\n",
                (unsigned)cursor, (unsigned long)event.seq,
                (unsigned long)event.ms, (unsigned)event.kind,
                (unsigned long)event.aux, (unsigned long)event.esr,
                (unsigned long)event.id, (unsigned)event.dlc,
                (unsigned)event.status, hex);
            if (write_reply(out, n)) { ++cursor; }
            return true; /* Retry the same event if UART rejected this line. */
        }
    } else { return false; }

    if (!write_reply(out, n)) {
        static const char error[] = "ERR ec format_or_uart\r\n";
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            (void)BoardConsole_Write((const uint8_t *)error, sizeof(error) - 1U);
        }
    }
    return true;
}

void __wrap_clip_cmd_parse_line(const struct clip *self, const struct clip_command *cmd,
                                char *line, void *context)
{
    /* The existing console dispatcher reserves a reply slot. These commands
     * inspect or freeze RAM only; they never change motor output. */
    if (cmd == NULL && line != NULL && command(line)) { return; }
    __real_clip_cmd_parse_line(self, cmd, line, context);
}
