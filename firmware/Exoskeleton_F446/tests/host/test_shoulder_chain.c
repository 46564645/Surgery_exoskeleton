/* Full software path with synthetic GPIO/CAN only. No physical acceptance. */
#include "test_shoulder_chain.h"
#include "test_console_probe.h"
#include "app_main.h"
#include "s3_bench.h"
#include "s3_site_mock.h"
#include "s3_shoulder_service.h"
#include "s3_feedback.h"
#include "test_s3.h"
#include "board_can_tx.h"
#include "can_monitor.h"
#include "can_tx_mock.h"
#include "shoulder_read_diagnostic.h"
#include "encoder_stream.h"
#include "stop_input_hal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void mailboxes(void)
{
    TestCanTx_Reset(); S3ShoulderService_Init(NULL, SHOULDER_READER_S3);
    BoardCanFrame read, command[2] = {{0},{0}};
    assert(GdsSdoRead_PropertyRequest(GDS_PROPERTY_POSITION, &read));
    command[0].id=0x02EU; command[0].dlc=4U;
    assert(BoardCanTx_SubmitShoulderRead(&read));
    assert(BoardCanTx_SubmitOutputAxes(command,1U,false));
    assert(BoardCanTx_PurgeOutputAxes(1U));
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER,0U)->id==0x024U);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER,1U)==NULL);
    assert(!BoardCanTx_SubmitAxes(command,1U,false)); /* Legacy all-empty contract. */
    assert(!BoardCanTx_SubmitShoulderRead(&read));
    assert(BoardCanTx_CancelShoulderRead());
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER,0U)==NULL);
    read.data[0]=1U; assert(!BoardCanTx_SubmitShoulderRead(&read)); read.data[0]=0U;
    assert(BoardCanTx_SubmitShoulderRead(&read));
    TestCanTx_CompleteOnAdd(BOARD_CAN_SHOULDER);
    assert(BoardCanTx_SubmitOutputAxes(command,1U,false));
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER,0U)->id==0x02EU);
    assert(BoardCanTx_CancelShoulderRead()); /* Reused output slot is not a read. */
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER,0U)->id==0x02EU);
    assert(BoardCanTx_PurgeOutputAxes(1U));
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER,0U)==NULL);
    assert(BoardCanTx_SubmitShoulderRead(&read));
    hcan1.State=HAL_CAN_STATE_ERROR; assert(!BoardCanTx_CancelShoulderRead());
    hcan1.State=HAL_CAN_STATE_LISTENING; assert(BoardCanTx_CancelShoulderRead());
    assert(BoardCanTx_SubmitShoulderRead(&read));
    TestCanTx_Busy(BOARD_CAN_SHOULDER,false);
    assert(!S3ShoulderService_Stop());
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    assert(!S3ShoulderService_Stop()); /* Failure stays latched after later completion. */
}
static uint32_t tick;
static uint8_t life, axis;
static bool recording;
static float torque, position;
static uint8_t control_mode, input_mode;
static unsigned read_count, output_count, idle_count;
static bool omit_reply, supervised, drift_after_stop, relative;
static bool calibrated, encoder_ready;
static float native_velocity;
static uint32_t float_bits(float value) { uint32_t b; memcpy(&b,&value,4U); return b; }
static void heartbeat(uint8_t fault)
{
    uint8_t data[8]={0}; data[4]=axis; data[5]=fault; data[7]=++life;
    TestCan_InjectData(BOARD_CAN_SHOULDER,0x021U,8U,0U,0U,data);
}
static S3SiteProfile site(void)
{
    S3SiteProfile p={0}; p.safety=TestS3_ConfirmedConfig();
    p.safety.enabled_axes=SAFETY_SHOULDER_AXES; p.safety.feedback_timeout_ms=50U;
    p.safety.service_limit_ms=5U; p.safety.max_request_ms=20U;
    p.safety.joint[0].mapping=(JointMapping){1.0f,0.0f,1,true};
    p.safety.joint[0].lower_rad=0.0f; p.safety.joint[0].upper_rad=10.0f;
    p.safety.joint[0].max_motor_command=0.05f; p.safety.joint[0].motor_slew_per_s=1.0f;
    p.shoulder_node=p.elbow_id=1U; p.prepare_timeout_ms=200U;
    p.trusted_feedback_axes=SAFETY_SHOULDER_AXES; p.coordinates[0]=S3_BENCH_ROTOR;
    p.rotor_reference_confirmed[0]=true;
    p.shoulder_source=S3_SHOULDER_ENCODER_PROPERTIES;
    p.shoulder_property_reply_ms=10U; p.shoulder_property_period_ms=10U;
    p.shoulder_property_window_ms=500U;
    p.stop_input_confirmed=true;
    p.operator_stop_input=(BoardStopInputConfig){BOARD_STOP_PORT_C,1U,true};
    p.independent_stop_input=(BoardStopInputConfig){BOARD_STOP_PORT_C,2U,true};
    if (supervised) {
        p.safety.stop_policy=SAFETY_STOP_SUPERVISED_SHOULDER_BENCH;
        p.stop_input_confirmed=false;
        p.operator_stop_input=(BoardStopInputConfig){0}; p.independent_stop_input=(BoardStopInputConfig){0};
        p.shoulder_standstill=(ShoulderStandstillConfig){0.001f,0.02f,20U,40U};
        p.shoulder_stop_window_ms=100U;
    }
    if (relative) {
        p.coordinates[0]=S3_BENCH_RELATIVE_ROTOR;
        p.rotor_reference_confirmed[0]=false;
        p.safety.joint[0].limits_confirmed=false;
        p.safety.joint[0].relative_native_envelope=true;
        p.safety.joint[0].software_envelope_approved=true;
        p.safety.joint[0].lower_rad=-0.034906585f;
        p.safety.joint[0].upper_rad=0.034906585f;
    }
    return p; /* All constants in this function are synthetic TEST-only values. */
}
static void initialize(void)
{
    S3SiteProfile p=site(); TestS3Site_ConfigureShoulderChain(&p);
    TestStopInputMock_Reset(); TestStopInputMock_SetClock(2U,!supervised);
    TestStopInputMock_GPIO(2U)->IDR=2U; /* Stop not requested; separate stop-complete input. */
    tick=100U; life=0U; axis=1U; torque=0.125f; position=1.0f;
    control_mode=3U; input_mode=3U; read_count=output_count=idle_count=0U; omit_reply=false; drift_after_stop=false;
    calibrated=encoder_ready=true; native_velocity=0.0f;
    TestClock_Set(tick); App_Init(); TestCanTx_Reset();
    heartbeat(0U); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER)==0U); /* No automatic startup TX. */
}
static void start(void)
{
    initialize();
    if (recording) {
        (void)TestConsole_QueryCached("s3limits\n");
        (void)TestConsole_QueryCached("s3timing\n");
        (void)TestConsole_QueryCached("s3stopcfg\n");
        assert(strstr(TestConsole_QueryCached("outputstatus\n"), "s3_state=0"));
        assert(strstr(TestConsole_QueryCached("prepare\n"), "OK preparing"));
    } else { assert(S3Bench_Prepare(tick)); }
    ShoulderReadStatus owner; ShoulderRead_GetStatus(&owner); assert(owner.phase==SHOULDER_READ_S3_OWNED);
    assert(!ShoulderRead_Start(GDS_READ_POSITION,tick) && !EncoderStream_Start(tick));
    S3BenchStatus gate; S3Bench_GetStatus(&gate); assert(gate.feedback_pending && !gate.output_allowed);
}
static void device_frame(const BoardCanFrame *f)
{
    if (f->id==0x024U) {
        read_count++;
        assert(f->dlc==8U && f->data[0]==0U);
        if (omit_reply) { return; }
        uint16_t ep=(uint16_t)f->data[1]|(uint16_t)((uint16_t)f->data[2]<<8);
        uint32_t bits=0U;
        switch(ep) {
        case 372U: bits=float_bits(position); break;
        case 378U: bits=float_bits(relative ? native_velocity : (axis==8U && torque!=0.0f ? 0.01f : 0.0f)); break;
        case 195U: bits=calibrated ? 1U : 0U; break;
        case 366U: bits=encoder_ready ? 1U : 0U; break;
        case 272U: bits=float_bits(torque); break;
        case 287U: bits=control_mode; break;
        case 288U: bits=input_mode; break;
        default: assert(false);
        }
        uint8_t data[8]={0}; data[1]=f->data[1]; data[2]=f->data[2];
        for(unsigned i=0U;i<4U;i++){data[4U+i]=(uint8_t)(bits>>(i*8U));}
        TestCan_InjectData(BOARD_CAN_SHOULDER,0x025U,8U,0U,0U,data);
    } else if(f->id==0x02EU) {
        memcpy(&torque,f->data,4U); output_count++;
    } else if(f->id==0x02BU) {
        control_mode=f->data[0]; input_mode=f->data[4]; output_count++;
    } else if(f->id==0x027U) {
        axis=f->data[0]; output_count++;
        if(axis==1U){idle_count++;TestStopInputMock_GPIO(2U)->IDR|=2U;}
        else {assert(axis==8U);TestStopInputMock_GPIO(2U)->IDR&=~2U;}
    } else {assert(false);}
}
static void step(void)
{
    tick++; TestClock_Set(tick);
    BoardCanFrame frames[3]; unsigned n=0U;
    for(unsigned i=0U;i<3U;i++){
        const BoardCanFrame *p=TestCanTx_Pending(BOARD_CAN_SHOULDER,i); if(p!=NULL){frames[n++]=*p;}
    }
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    /* bxCAN uses identifier priority: process the read before the higher IDs. */
    for(unsigned i=0U;i<n;i++)for(unsigned j=i+1U;j<n;j++)if(frames[j].id<frames[i].id){BoardCanFrame x=frames[i];frames[i]=frames[j];frames[j]=x;}
    for(unsigned i=0U;i<n;i++){device_frame(&frames[i]);}
    if(axis==8U && torque!=0.0f){position+=0.0001f;}
    if (axis==1U && idle_count>0U && drift_after_stop) { position+=0.002f; }
    heartbeat(0U); TestCan_Interrupt(BOARD_CAN_SHOULDER);
    App_Poll();
}
static void ready(void)
{
    S3BenchStatus gate;
    for(unsigned i=0U;i<150U;i++){
        step(); S3Bench_GetStatus(&gate);
        if(gate.state==OUTPUT_READY){assert(!gate.feedback_pending && read_count>=21U);return;}
        assert(gate.state!=OUTPUT_FAULT);
    }
    assert(false);
}
static void run(void)
{
    start(); ready(); S3SiteSample sample;
    assert(S3Site_ReadSample(tick,&sample) && sample.driver[0].calibration_verified);
    assert(sample.driver[0].zero_verified && sample.driver[0].mode_verified && !sample.independently_stopped);
    assert(S3Bench_Arm(tick)); S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(gate.arm_token!=0U && S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick));
    unsigned before=read_count;
    for(unsigned i=0U;i<22U;i++){step();}
    S3Bench_GetStatus(&gate);
    assert(read_count>before && output_count>10U && position>1.0f);
    assert(gate.state==OUTPUT_FAULT && !gate.output_allowed && gate.stop_accepted && idle_count==1U);
    assert(S3ShoulderService_Get().state==S3_READER_STOPPED);
    unsigned stopped=read_count; for(unsigned i=0U;i<5U;i++){step();} assert(read_count==stopped);
    assert(!S3Bench_Prepare(tick)); /* No automatic restart after the bounded run. */
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW)==0U);
    printf("S3_CHAIN synthetic reads=%u outputs=%u idle=%u pulse_ttl_ms=20 stopped_no_restart=1\n",
           read_count,output_count,idle_count);
}
static void faults(void)
{
    start(); omit_reply=true;
    for(unsigned i=0U;i<15U;i++){step();}
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && !gate.output_allowed && read_count==1U && output_count==0U);
    assert(S3ShoulderService_Get().state==S3_READER_FAULT);
    start(); ready();
    assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    /* Bad then good heartbeat in the SAME RX batch may not restore permission. */
    tick++; TestClock_Set(tick); heartbeat(1U); heartbeat(0U);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll(); S3Bench_Poll(tick,1U);
    S3Bench_GetStatus(&gate); assert(gate.state==OUTPUT_FAULT && !gate.output_allowed);
    assert(S3ShoulderService_Get().state==S3_READER_FAULT);
    start(); ready();
    assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    TestStopInputMock_GPIO(2U)->IDR|=1U; step();
    S3Bench_GetStatus(&gate); assert(gate.state==OUTPUT_FAULT && !gate.output_allowed && gate.stop_accepted);
    start(); S3Bench_Cancel();
    assert(!S3Bench_Prepare(tick) && S3ShoulderService_Get().state==S3_READER_STOPPED);
    start(); ready(); TestCanTx_Busy(BOARD_CAN_SHOULDER,false);
    assert(!S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && S3ShoulderService_Get().state!=S3_READER_ACTIVE);
    start(); step(); /* Reader owns a queued request; gateway is still SAFE_OFF. */
    TestCanTx_Busy(BOARD_CAN_SHOULDER,false);
    S3Bench_Cancel(); S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && S3ShoulderService_Get().state==S3_READER_FAULT);
}
static void supervised_run(void)
{
    start(); ready(); S3BenchStatus gate; S3SiteSample sample;
    assert(S3Site_ReadSample(tick,&sample) && !sample.operator_stop_clear && !sample.independently_stopped);
    assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick));
    for(unsigned i=0U;i<20U;i++){step();}
    S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && gate.stop_accepted && !gate.output_allowed);
    assert(S3ShoulderService_Get().state==S3_READER_OBSERVING);
    assert(!S3ShoulderService_Get().stop_observed && !S3ShoulderService_GetStandstill(tick).stationary);
    unsigned before=read_count, commands=output_count;
    step(); /* Pending pre-stop read, if any, retains association and cannot certify stop. */
    assert(!S3ShoulderService_Get().stop_observed);
    for(unsigned i=0U;i<80U && S3ShoulderService_Get().state==S3_READER_OBSERVING;i++){step();}
    S3ReaderStatus reader=S3ShoulderService_Get();
    assert(reader.state==S3_READER_COMPLETE && reader.stop_observed);
    assert(reader.observed_ms-reader.stop_ms>=20U && read_count>before);
    assert(output_count==commands+1U && idle_count==1U); /* Only the pending IDLE, then reads. */
    assert(!S3Bench_Prepare(tick) && !S3Bench_Arm(tick) && !S3Bench_AcknowledgeFault(tick));
    unsigned done=read_count; for(unsigned i=0U;i<50U;i++){step();}
    assert(read_count==done && S3ShoulderService_Get().stop_observed);
    assert(!S3ShoulderService_GetStandstill(tick).fresh); /* Historical result stays separate. */
    printf("S3_SUPERVISED synthetic reads=%u outputs=%u idle=%u observed_after_ms=%lu boot_once=1 gpio_reads_not_required=1\n",
        read_count,output_count,idle_count,(unsigned long)(reader.observed_ms-reader.stop_ms));
}
static void supervised_faults(void)
{
    S3BenchStatus gate;
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick));
    for(unsigned i=0U;i<20U;i++){step();}
    drift_after_stop=true;
    for(unsigned i=0U;i<105U;i++){step();}
    assert(S3ShoulderService_Get().state==S3_READER_FAULT && !S3ShoulderService_Get().stop_observed);
    assert(S3ShoulderService_Get().reason==S3_READ_WINDOW);
    assert(!S3Bench_Prepare(tick));
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    TestCanTx_Fail(BOARD_CAN_SHOULDER,true,false); S3Bench_Cancel(); S3Bench_GetStatus(&gate);
    assert(gate.stop_delivery_failed && !gate.stop_accepted && gate.state==OUTPUT_FAULT);
    assert(S3ShoulderService_Get().state!=S3_READER_OBSERVING && !S3ShoulderService_Get().stop_observed);
    TestCanTx_Fail(BOARD_CAN_SHOULDER,false,false);
    assert(!S3Bench_AcknowledgeFault(tick) && !S3Bench_Prepare(tick));
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    TestCanTx_Fail(BOARD_CAN_SHOULDER,false,true); S3Bench_Cancel(); S3Bench_GetStatus(&gate);
    assert(gate.stop_delivery_failed && !gate.purge_ok && gate.stop_accepted);
    assert(S3ShoulderService_Get().state!=S3_READER_OBSERVING && !S3ShoulderService_Get().stop_observed);
    TestCanTx_Fail(BOARD_CAN_SHOULDER,false,false); TestCanTx_Complete(BOARD_CAN_SHOULDER);
    assert(!S3Bench_AcknowledgeFault(tick) && !S3Bench_Prepare(tick));
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    S3Bench_Cancel(); assert(S3ShoulderService_Get().state==S3_READER_OBSERVING);
    omit_reply=true; for(unsigned i=0U;i<20U;i++){step();}
    assert(S3ShoulderService_Get().state==S3_READER_FAULT && !S3ShoulderService_Get().stop_observed);
    assert(!S3Bench_Prepare(tick));
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    TestCanTx_FailNext(BOARD_CAN_SHOULDER);
    tick++; TestClock_Set(tick); S3Bench_Poll(tick,1U); S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && gate.reason==OUTPUT_REASON_TRANSPORT && gate.stop_accepted);
    assert(S3ShoulderService_Get().state!=S3_READER_OBSERVING);
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    assert(S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); step();
    tick+=6U; TestClock_Set(tick); S3Bench_Poll(tick,6U); S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && !gate.output_allowed);
    assert(S3ShoulderService_Get().state!=S3_READER_OBSERVING);
    start(); ready(); assert(S3Bench_Arm(tick)); S3Bench_GetStatus(&gate);
    tick+=6U; TestClock_Set(tick);
    assert(!S3Bench_Request(gate.arm_token,0U,0.02f,20U,tick)); S3Bench_GetStatus(&gate);
    assert(gate.state==OUTPUT_FAULT && gate.stop_accepted);
    assert(S3ShoulderService_Get().state!=S3_READER_OBSERVING);
}
static void recording_run(void)
{
    start(); ready();
    unsigned tx = TestCanTx_Sent(BOARD_CAN_SHOULDER);
    const char *line = TestConsole_QueryCached("s3properties\n");
    assert(strstr(line, "tq=00000000 cal=1 ready=1 cm=1 im=1")); printf("S3_RECORD %s", line);
    assert(strstr(TestConsole_QueryCached("outputstatus\n"), "s3_state=5"));
    line = TestConsole_QueryCached("s3feedback\n"); assert(strstr(line, "fresh=1")); printf("S3_RECORD %s", line);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == tx);
    unsigned long token = 0U;
    assert(sscanf(TestConsole_QueryCached("arm\n"), "OK armed token=%lu", &token) == 1 && token != 0U);
    char command[64]; (void)snprintf(command, sizeof(command), "shoulderpulse %lu 20 20\n", token);
    assert(strstr(TestConsole_QueryCached(command), "OK request"));
    for (unsigned i = 0U; i < 20U; i++) {
        step();
        if (i % 5U == 4U) {
            tx = TestCanTx_Sent(BOARD_CAN_SHOULDER);
            printf("S3_RECORD %s", TestConsole_QueryCached("s3feedback\n"));
            assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == tx);
        }
    }
    for (unsigned i = 0U; i < 80U && S3ShoulderService_Get().state == S3_READER_OBSERVING; i++) { step(); }
    assert(S3ShoulderService_Get().stop_observed && idle_count == 1U && position > 1.0f);
    tx = TestCanTx_Sent(BOARD_CAN_SHOULDER);
    line = TestConsole_QueryCached("s3reader\n"); assert(strstr(line, "observed=1")); printf("S3_RECORD %s", line);
    line = TestConsole_QueryCached("stopstatus\n"); assert(strstr(line, "observed=1")); printf("S3_RECORD %s", line);
    line = TestConsole_QueryCached("s3feedback\n"); assert(strstr(line, "fresh=0")); printf("S3_RECORD %s", line);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == tx && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    assert(!S3Bench_Prepare(tick) && !S3Bench_Arm(tick));
}
static void assert_anchor_evidence(S3RelativeAnchor expected)
{
    S3RelativeAnchor actual=S3Feedback_GetAnchor();
    assert(actual.origin_rev==expected.origin_rev && actual.sequence==expected.sequence);
    assert(actual.request_ms==expected.request_ms && actual.reply_ms==expected.reply_ms);
    assert(actual.begin_ms==expected.begin_ms);
}
/* Submit/associate/reply through the real HAL seam, without polling the gateway.
 * This lets passive-query and timestamp tests inspect a just-published cycle. */
static void publish_relative_cycle(void)
{
    for(unsigned field=0U;field<GDS_PROPERTY_COUNT;field++){
        BoardCanFrame request;
        assert(GdsSdoRead_PropertyRequest((GdsPropertyField)field,&request));
        assert(BoardCanTx_SubmitShoulderRead(&request));
        assert(CanMonitor_ExpectShoulderProperty((GdsPropertyField)field,tick));
        tick++; TestClock_Set(tick);
        TestCanTx_Complete(BOARD_CAN_SHOULDER); device_frame(&request); heartbeat(0U);
        TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
        tick++; TestClock_Set(tick);
    }
}
static void assert_relative_waiting(void)
{
    S3SiteSample sample;
    assert(!S3Feedback_Read(S3Site_GetProfile(),tick,&sample) && !sample.joint[0].valid);
    S3RelativeAnchor a=S3Feedback_GetAnchor();
    assert(a.phase==S3_RELATIVE_WAITING && a.sequence==0U && a.origin_rev==0.0f);
    assert(output_count==0U);
}
static void relative_anchor(void)
{
    start(); torque=0.0f;
    S3RelativeAnchor waiting=S3Feedback_GetAnchor();
    assert(waiting.phase==S3_RELATIVE_WAITING && waiting.begin_ms==tick);
    assert(!S3Feedback_BeginRelative(tick));
    for(unsigned i=0U;i<30U && S3Feedback_GetAnchor().phase==S3_RELATIVE_WAITING;i++){step();}
    S3RelativeAnchor anchor=S3Feedback_GetAnchor();
    assert(anchor.phase==S3_RELATIVE_ACTIVE && anchor.origin_rev==1.0f && anchor.sequence==1U);
    assert(anchor.request_ms>anchor.begin_ms && anchor.reply_ms>anchor.request_ms);
    S3SiteSample sample;
    assert(S3Site_ReadSample(tick,&sample) && sample.joint[0].position_rad==0.0f);
    assert(anchor.request_ms==sample.shoulder_properties.requested_ms[GDS_PROPERTY_POSITION]);
    assert(anchor.reply_ms==sample.shoulder_properties.received_ms[GDS_PROPERTY_POSITION]);
    ready();
    position=anchor.origin_rev+0.002f; native_velocity=-0.002f;
    for(unsigned i=0U;i<20U;i++){step();}
    assert(S3Site_ReadSample(tick,&sample));
    assert(fabsf(sample.joint[0].position_rad-(position-anchor.origin_rev)*6.283185307f)<0.000001f);
    assert(sample.joint[0].position_rad>0.01f);
    assert(fabsf(sample.joint[0].velocity_rad_s-native_velocity*6.283185307f)<0.000001f);
    assert_anchor_evidence(anchor); assert(!S3Feedback_BeginRelative(tick));
    native_velocity=0.0f;
    S3Bench_Cancel();
    assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_RETIRED); assert_anchor_evidence(anchor);
    assert(!S3Bench_Prepare(tick) && !S3Feedback_BeginRelative(tick));
    position=anchor.origin_rev+0.003f;
    for(unsigned i=0U;i<5U;i++){step();}
    (void)S3Feedback_Read(S3Site_GetProfile(),tick,&sample);
    S3Feedback_EndRelative(); assert_anchor_evidence(anchor);
    assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_RETIRED);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW)==0U);
    /* Cancellation before any eligible snapshot is also terminal for this boot. */
    start(); waiting=S3Feedback_GetAnchor(); S3Bench_Cancel();
    assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_RETIRED); assert_anchor_evidence(waiting);
    assert(!S3Bench_Prepare(tick) && !S3Feedback_BeginRelative(tick));
    printf("S3_RELATIVE fixed_origin=1 native_speed=1 cancellation_terminal=1 evidence_retained=1\n");
}
static void relative_rejects(void)
{
    initialize(); torque=0.0f; control_mode=input_mode=1U;
    publish_relative_cycle();
    S3SiteSample sample;
    assert(!S3Feedback_Read(S3Site_GetProfile(),tick,&sample));
    assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_UNSET);
    assert(!S3Bench_Prepare(tick)); /* Unowned pre-prepare replies cannot be reclaimed. */
    initialize(); torque=0.0f; control_mode=input_mode=1U;
    assert(S3Bench_Prepare(tick));
    assert_relative_waiting();
    publish_relative_cycle();
    assert_relative_waiting(); /* First request equals begin; strictly later is required. */
    axis=8U; publish_relative_cycle(); assert_relative_waiting();
    axis=1U; torque=0.125f; publish_relative_cycle(); assert_relative_waiting();
    torque=0.0f; calibrated=false; publish_relative_cycle(); assert_relative_waiting();
    calibrated=true; encoder_ready=false; publish_relative_cycle(); assert_relative_waiting();
    encoder_ready=true; publish_relative_cycle();
    tick+=51U; TestClock_Set(tick); heartbeat(0U); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert_relative_waiting(); /* An otherwise eligible complete cycle is stale. */
    position=5.0f; publish_relative_cycle();
    assert(S3Feedback_Read(S3Site_GetProfile(),tick,&sample) && sample.joint[0].position_rad==0.0f);
    S3RelativeAnchor anchor=S3Feedback_GetAnchor();
    assert(anchor.phase==S3_RELATIVE_ACTIVE && anchor.origin_rev==5.0f);
    assert(anchor.sequence==sample.shoulder_properties.sequence);
    assert(anchor.request_ms>anchor.begin_ms && anchor.reply_ms>anchor.begin_ms);
    CanMonitor_InvalidateShoulderProperties();
    S3Bench_Poll(tick,1U);
    assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_RETIRED); assert_anchor_evidence(anchor);
    assert(!S3Bench_Prepare(tick) && !S3Feedback_BeginRelative(tick));
    printf("S3_RELATIVE rejects=unprepared,preprepare,equal_begin,closed_loop,target,calibration,encoder,stale reader_fault_terminal=1\n");
}
static void relative_cli(void)
{
    initialize(); torque=0.0f; control_mode=input_mode=1U; publish_relative_cycle();
    unsigned tx=TestCanTx_Sent(BOARD_CAN_SHOULDER);
    assert(strstr(TestConsole_QueryCached("s3anchor\n"),"phase=0"));
    assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_UNSET);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER)==tx);
    initialize(); torque=0.0f; control_mode=input_mode=1U;
    assert(S3Bench_Prepare(tick)); tick++; TestClock_Set(tick);
    publish_relative_cycle();
    S3RelativeAnchor waiting=S3Feedback_GetAnchor();
    assert(waiting.phase==S3_RELATIVE_WAITING);
    ShoulderPropertySnapshot before,after;
    assert(CanMonitor_GetShoulderProperties(tick,50U,&before));
    S3ReaderStatus reader=S3ShoulderService_Get();
    tx=TestCanTx_Sent(BOARD_CAN_SHOULDER);
    char first[256]; const char *line=TestConsole_QueryCached("s3anchor\n");
    assert(strlen(line)<sizeof(first)); strcpy(first,line);
    assert(strstr(first,"OK s3anchor") && strstr(first,"phase=1"));
    assert(!strcmp(first,TestConsole_QueryCached("s3anchor\n")));
    assert_anchor_evidence(waiting); assert(S3Feedback_GetAnchor().phase==S3_RELATIVE_WAITING);
    assert(S3ShoulderService_Get().requests==reader.requests);
    assert(CanMonitor_GetShoulderProperties(tick,50U,&after) && after.sequence==before.sequence);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER)==tx && TestCanTx_Sent(BOARD_CAN_ELBOW)==0U);
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(!gate.output_allowed && gate.arm_token==0U && gate.feedback_pending);
    S3SiteSample sample; assert(S3Site_ReadSample(tick,&sample));
    S3RelativeAnchor anchor=S3Feedback_GetAnchor(); assert(anchor.phase==S3_RELATIVE_ACTIVE);
    assert(strstr(TestConsole_QueryCached("s3anchor\n"),"phase=2")); assert_anchor_evidence(anchor);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER)==tx);
    printf("S3_RELATIVE s3anchor_passive=1 qualifying_snapshot_query_does_not_latch=1\n");
}
static void assert_site_rejected(S3SiteProfile profile)
{
    TestS3Site_ConfigureShoulderChain(&profile); S3Bench_Init();
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(!gate.configured && !gate.transport_bound && !gate.output_allowed);
    assert(!S3Bench_Prepare(tick));
}
static void relative_config(void)
{
    initialize(); S3SiteProfile profile=site(); SafetyConfig valid=profile.safety,c;
    assert(!profile.rotor_reference_confirmed[0] && !valid.joint[0].limits_confirmed);
    assert(SafetySupervisor_ConfigReady(&valid) && S3ShoulderService_ProfileReady(&profile));
    c=valid; c.joint[0].software_envelope_approved=false; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.joint[0].limits_confirmed=true; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.joint[0].mapping.direction=-1; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.joint[0].mapping.rotor_per_joint=2.0f; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.joint[0].mapping.rotor_zero_rad=1.0f; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.joint[0].mapping.confirmed=false; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.stop_policy=SAFETY_STOP_EXTERNAL_INPUT; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.enabled_axes=SAFETY_ALL_AXES; assert(!SafetySupervisor_ConfigReady(&c));
    c=valid; c.stop_policy=SAFETY_STOP_SUPERVISED_C620_BENCH; c.enabled_axes=SAFETY_ELBOW_AXES;
    c.joint[1]=c.joint[0]; assert(!SafetySupervisor_ConfigReady(&c));
    /* Approval of a software envelope never substitutes for old limits evidence. */
    c=TestS3_ConfirmedConfig(); c.joint[0].limits_confirmed=false;
    c.joint[0].software_envelope_approved=true; assert(!SafetySupervisor_ConfigReady(&c));
    c.joint[0].limits_confirmed=true; assert(!SafetySupervisor_ConfigReady(&c));
    c.joint[0].software_envelope_approved=false; assert(SafetySupervisor_ConfigReady(&c));
    for(unsigned coordinates=S3_BENCH_ROTOR;coordinates<=S3_ASSEMBLED_JOINT;coordinates++){
        S3SiteProfile mismatch=profile; mismatch.coordinates[0]=(S3CoordinateScope)coordinates;
        assert_site_rejected(mismatch);
        mismatch.safety.joint[0].relative_native_envelope=false;
        assert_site_rejected(mismatch); /* Old coordinates still need limits_confirmed. */
    }
    S3SiteProfile mismatch=profile; mismatch.safety.joint[0].relative_native_envelope=false;
    mismatch.safety.joint[0].limits_confirmed=true; assert_site_rejected(mismatch);
    mismatch=profile; mismatch.rotor_reference_confirmed[0]=true; assert_site_rejected(mismatch);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER)==0U && TestCanTx_Sent(BOARD_CAN_ELBOW)==0U);
    printf("S3_RELATIVE config_scope=shoulder_only old_limits_required=1 coordinate_flag_consistent=1\n");
}
void TestShoulderChain_Run(const char *name)
{
    recording=!strcmp(name,"s3_chain_recording");
    relative=!strncmp(name,"s3_chain_relative_",18);
    supervised=relative || recording || !strncmp(name,"s3_chain_supervised",19);
    if(recording){recording_run();}
    else if(!strcmp(name,"s3_chain_relative_anchor")){relative_anchor();}
    else if(!strcmp(name,"s3_chain_relative_rejects")){relative_rejects();}
    else if(!strcmp(name,"s3_chain_relative_cli")){relative_cli();}
    else if(!strcmp(name,"s3_chain_relative_config")){relative_config();}
    else if(!strcmp(name,"s3_chain_supervised")){supervised_run();}
    else if(!strcmp(name,"s3_chain_supervised_faults")){supervised_faults();}
    else if(!strcmp(name,"s3_chain_mailboxes")){mailboxes();}
    else if(!strcmp(name,"s3_chain_run")){run();}
    else {assert(!strcmp(name,"s3_chain_faults"));faults();}
}
