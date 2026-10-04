/* Fixed CLIP query callbacks; reuse provider snapshots and bounded S1 replies. */
#include "s3_console.h"
#include "s3_site.h"
#include "s3_feedback.h"
#include "s3_bench.h"
#include "s3_shoulder_service.h"
#include "shoulder_qualification.h"
#include "can_monitor.h"
#include "board_console.h"
#include "s1_config.h"
#include <stdio.h>
#include <string.h>

static unsigned allowed(void)
{ S3BenchStatus s; S3Bench_GetStatus(&s); return (unsigned)s.output_allowed; }
static unsigned long bits(float value)
{ uint32_t raw; memcpy(&raw, &value, sizeof(raw)); return (unsigned long)raw; }
static void emit(const char *out, int length)
{
    static const char error[] = "ERR format\r\n";
    if (length > 0 && (unsigned)length < S1_REPLY_SIZE) {
        (void)BoardConsole_Write((const uint8_t *)out, (size_t)length);
    } else { (void)BoardConsole_Write((const uint8_t *)error, sizeof(error) - 1U); }
}
static void hex_frame(const BoardCanFrame *f, char out[17])
{
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0U; i < 8U; i++) {
        out[2U*i] = hex[f->data[i] >> 4]; out[2U*i+1U] = hex[f->data[i] & 15U];
    }
    out[16] = '\0';
}
void S3Console_QualificationField(unsigned field)
{
    char out[S1_REPLY_SIZE];
    if (field >= GDS_PROPERTY_COUNT) { emit("ERR args\r\n", 10); return; }
    ShoulderQualificationStatus q; ShoulderQualification_GetStatus(&q);
    const ShoulderQualificationSample *s = &q.sample[field];
    char request[17], response[17]; hex_frame(&s->request, request); hex_frame(&s->reply, response);
    int n = snprintf(out, sizeof(out),
        "OK qualfield field=%u ep=%u requested=%u replied=%u decoded=%u q=%lu r=%lu "
        "bits=%08lX req=%s raw=%s id=%03X dlc=%u\r\n",
        field, (unsigned)GdsSdoRead_PropertyEndpoint((GdsPropertyField)field),
        (unsigned)s->requested, (unsigned)s->replied, (unsigned)s->decoded,
        (unsigned long)s->request_ms, (unsigned long)s->reply_ms,
        (unsigned long)s->raw_value_bits, request, response,
        (unsigned)s->reply.id, (unsigned)s->reply.dlc);
    emit(out, n);
}
bool S3Console_Command(const char *command, uint32_t now_ms)
{
    char out[S1_REPLY_SIZE]; int n;
    const S3SiteProfile *p = S3Site_GetProfile();
    if (!strcmp(command, "s3feedback") || !strcmp(command, "s3properties")) {
        ShoulderPropertySnapshot s;
        bool fresh = CanMonitor_GetShoulderProperties(now_ms,
            p == NULL ? 0U : p->safety.feedback_timeout_ms, &s);
        if (!strcmp(command, "s3feedback")) {
            n = snprintf(out, sizeof(out),
                "OK s3feedback seq=%lu now=%lu fresh=%u pos=%08lX vel=%08lX "
                "pq=%lu pr=%lu vq=%lu vr=%lu out=%u\r\n",
                (unsigned long)s.sequence, (unsigned long)now_ms, (unsigned)fresh,
                bits(s.value[GDS_PROPERTY_POSITION].real), bits(s.value[GDS_PROPERTY_VELOCITY].real),
                (unsigned long)s.requested_ms[GDS_PROPERTY_POSITION], (unsigned long)s.received_ms[GDS_PROPERTY_POSITION],
                (unsigned long)s.requested_ms[GDS_PROPERTY_VELOCITY], (unsigned long)s.received_ms[GDS_PROPERTY_VELOCITY], allowed());
        } else {
            n = snprintf(out, sizeof(out),
                "OK s3properties seq=%lu now=%lu fresh=%u tq=%08lX cal=%u ready=%u cm=%u im=%u "
                "zq=%lu mq=%lu iq=%lu first=%lu last=%lu\r\n",
                (unsigned long)s.sequence, (unsigned long)now_ms, (unsigned)fresh,
                bits(s.value[GDS_PROPERTY_INPUT_TORQUE].real), (unsigned)s.value[GDS_PROPERTY_MOTOR_CALIBRATED].byte,
                (unsigned)s.value[GDS_PROPERTY_ENCODER_READY].byte, (unsigned)s.value[GDS_PROPERTY_CONTROL_MODE].byte,
                (unsigned)s.value[GDS_PROPERTY_INPUT_MODE].byte, (unsigned long)s.requested_ms[GDS_PROPERTY_INPUT_TORQUE],
                (unsigned long)s.requested_ms[GDS_PROPERTY_CONTROL_MODE], (unsigned long)s.requested_ms[GDS_PROPERTY_INPUT_MODE],
                (unsigned long)s.requested_ms[GDS_PROPERTY_POSITION], (unsigned long)s.received_ms[GDS_PROPERTY_INPUT_MODE]);
        }
    } else if (!strcmp(command, "s3anchor")) {
        S3RelativeAnchor a = S3Feedback_GetAnchor();
        n = snprintf(out, sizeof(out),
            "OK s3anchor coords=%u phase=%u pos=%08lX seq=%lu q=%lu r=%lu begin=%lu now=%lu out=%u\r\n",
            p == NULL ? 0U : (unsigned)p->coordinates[0], (unsigned)a.phase, bits(a.origin_rev),
            (unsigned long)a.sequence, (unsigned long)a.request_ms, (unsigned long)a.reply_ms,
            (unsigned long)a.begin_ms, (unsigned long)now_ms, allowed());
    } else if (!strcmp(command, "s3reader")) {
        S3ReaderStatus s = S3ShoulderService_Get(); GdsFeedback hb = {0}; bool wire;
        (void)CanMonitor_GetShoulderObservation(&hb, &wire);
        n = snprintf(out, sizeof(out),
            "OK s3reader state=%u reason=%u requests=%lu stop=%lu observed=%u obs=%lu "
            "hb=%u hms=%lu error=%08lX fault=%u win=%lu out=%u now=%lu hvalid=%u\r\n",
            (unsigned)s.state, (unsigned)s.reason, (unsigned long)s.requests, (unsigned long)s.stop_ms,
            (unsigned)s.stop_observed, (unsigned long)s.observed_ms, (unsigned)hb.axis_state,
            (unsigned long)hb.heartbeat_ms, (unsigned long)hb.axis_error, (unsigned)hb.fault_flags,
            (unsigned long)S3ShoulderService_WindowRemaining(now_ms), allowed(), (unsigned long)now_ms,
            (unsigned)(p != NULL && hb.has_heartbeat && hb.life_contiguous && hb.axis_error == 0U
                && hb.fault_flags == 0U && p->safety.feedback_timeout_ms > 0U
                && (uint32_t)(now_ms - hb.heartbeat_ms) <= p->safety.feedback_timeout_ms));
    } else if (!strcmp(command, "s3limits") && p != NULL) {
        const SafetyJointConfig *j = &p->safety.joint[0]; const JointMapping *m = &j->mapping;
        n = snprintf(out, sizeof(out),
            "OK s3limits axes=%u policy=%u coords=%u direction=%d ratio=%08lX zero=%08lX "
            "lo=%08lX hi=%08lX speed=%08lX limit=%08lX slew=%08lX\r\n",
            (unsigned)p->safety.enabled_axes, (unsigned)p->safety.stop_policy, (unsigned)p->coordinates[0],
            (int)m->direction, bits(m->rotor_per_joint), bits(m->rotor_zero_rad), bits(j->lower_rad),
            bits(j->upper_rad), bits(j->max_speed_rad_s), bits(j->max_motor_command), bits(j->motor_slew_per_s));
    } else if (!strcmp(command, "s3timing") && p != NULL) {
        n = snprintf(out, sizeof(out),
            "OK s3timing feedback=%lu service=%lu max_request=%lu prepare=%lu reply=%lu period=%lu window=%lu\r\n",
            (unsigned long)p->safety.feedback_timeout_ms, (unsigned long)p->safety.service_limit_ms,
            (unsigned long)p->safety.max_request_ms, (unsigned long)p->prepare_timeout_ms,
            (unsigned long)p->shoulder_property_reply_ms, (unsigned long)p->shoulder_property_period_ms,
            (unsigned long)p->shoulder_property_window_ms);
    } else if (!strcmp(command, "s3stopcfg") && p != NULL) {
        n = snprintf(out, sizeof(out), "OK s3stopcfg span=%08lX speed=%08lX dwell=%lu fresh=%lu window=%lu\r\n",
            bits(p->shoulder_standstill.span_rev), bits(p->shoulder_standstill.max_velocity_rev_s),
            (unsigned long)p->shoulder_standstill.dwell_ms, (unsigned long)p->shoulder_standstill.freshness_ms,
            (unsigned long)p->shoulder_stop_window_ms);
    } else if (!strcmp(command, "qualify")) {
        n = snprintf(out, sizeof(out), "%s\r\n", ShoulderQualification_Start(now_ms)
            ? "OK qualification_started output_allowed=0" : "ERR qualification_blocked");
    } else if (!strcmp(command, "qualcancel")) {
        n = snprintf(out, sizeof(out), "%s\r\n", ShoulderQualification_Cancel(now_ms)
            ? "OK qualification_cancelled output_allowed=0" : "ERR qualification_cancel");
    } else if (!strcmp(command, "qualstatus")) {
        ShoulderQualificationStatus q; ShoulderQualification_GetStatus(&q);
        n = snprintf(out, sizeof(out),
            "OK qualstatus state=%u reason=%u attempted=%u completed=%u mask=%u claimed=%u pending=%u "
            "first=%u last=%u begin=%lu end=%lu hfirst=%lu hlast=%lu cancelled=%u out=%u\r\n",
            (unsigned)q.state, (unsigned)q.reason, q.attempts, q.completed, (unsigned)q.completed_mask,
            (unsigned)q.claimed, (unsigned)q.pending, (unsigned)q.first_idle, (unsigned)q.last_idle,
            (unsigned long)q.started_ms, (unsigned long)q.finished_ms,
            (unsigned long)q.first_heartbeat.heartbeat_ms, (unsigned long)q.last_heartbeat.heartbeat_ms,
            (unsigned)q.cancel_accepted, allowed());
    } else if (!strcmp(command, "qualerror")) {
        ShoulderQualificationStatus q; ShoulderQualification_GetStatus(&q); char raw[17];
        hex_frame(&q.unexpected_frame, raw);
        n = snprintf(out, sizeof(out), "OK qualerror present=%u id=%03X dlc=%u rx=%lu raw=%s\r\n",
            (unsigned)q.has_unexpected, (unsigned)q.unexpected_frame.id, (unsigned)q.unexpected_frame.dlc,
            (unsigned long)q.unexpected_frame.received_ms, raw);
    } else if (!strcmp(command, "s3help")) {
        n = snprintf(out, sizeof(out),
            "OK commands=s3feedback,s3properties,s3reader,s3anchor,s3limits,s3timing,s3stopcfg,qualify,qualstatus,qualfield,qualerror,qualcancel\r\n");
    } else { return false; }
    emit(out, n); return true;
}
