"""Focused pure/fake-serial checks. All numbers are synthetic, never a live plan."""
import argparse
import json
import struct
import sys


def words(name, fields):
    return 'OK ' + name + ' ' + ' '.join(k + '=' + str(v) for k, v in fields.items())


def bits(value):
    return '%08X' % struct.unpack('<I', struct.pack('<f', value))[0]


def fixture():
    return {'version': 1, 'mode': 'trial', 'approved': True, 'enabled': True,
            's3limits': {'axes': '1', 'policy': '2', 'coords': '1', 'direction': '1',
                         'ratio': bits(1), 'zero': bits(0), 'lo': bits(-1), 'hi': bits(1),
                         'speed': bits(1), 'limit': bits(.02), 'slew': bits(1)},
            's3timing': {'feedback': '100', 'service': '5', 'max_request': '50', 'prepare': '200',
                         'reply': '20', 'period': '10', 'window': '1000'},
            's3stopcfg': {'span': bits(.001), 'speed': bits(.01), 'dwell': '20', 'fresh': '100', 'window': '350'},
            'pulse': {'signed_mNm': 10, 'ttl_ms': 40},
            'bounds': {'total_seconds': 2, 'prepare_seconds': .2, 'stop_seconds': .4, 'max_queries': 100},
            '_cycle_ms': 13}


def relative_fixture():
    plan = fixture()
    plan['s3limits']['coords'] = '3'
    return plan


class Clock:
    def __init__(self): self.value = 0.0
    def __call__(self): return self.value


class Journal:
    def __init__(self): self.events = []
    def emit(self, event, **values): self.events.append({'event': event, **values})


class Machine:
    def __init__(self, module, mode, case):
        self.m, self.mode, self.case = module, mode, case
        self.clock, self.commands, self.journal = Clock(), [], Journal()
        self.qual_started = self.prepared = self.pulsed = False
        self.output_reads = self.qual_reads = self.feedback_reads = self.reader_reads = 0
        self.plan = relative_fixture() if case.startswith('relative_') else fixture()

    def qual(self, state=0):
        values = dict(state=state, reason=0, attempted=0, completed=0, mask=0, claimed=0,
                      pending=0, first=0, last=0, begin=0, end=0, hfirst=0, hlast=0,
                      cancelled=0, out=0)
        if state == 1: values.update(attempted=1, claimed=1, pending=1, first=1, begin=100, hfirst=99)
        if state == 2: values.update(attempted=7, completed=7, mask=127, claimed=1, first=1,
                                    last=1, begin=100, end=121, hfirst=99, hlast=121, cancelled=1)
        if state == 3: values.update(reason=5, attempted=2, completed=1, mask=1, claimed=1,
                                    first=1, begin=100, end=110, hfirst=99, cancelled=1)
        return values

    def sample(self, sequence, now, request, position, fresh=1, out=0):
        if self.plan['s3limits']['coords'] == '3':
            position += 37.125  # Native absolute rotor estimate, separate from frozen origin.
        return dict(seq=sequence, now=now, fresh=fresh, pos=bits(position), vel=bits(.002),
                    pq=request, pr=request+1, vq=request+2, vr=request+3, out=out)

    def respond(self, command):
        self.commands.append(command)
        if command == 'ping': return 'OK pong'
        if command == 'status': return 'OK stage=S2_CAN_RX uptime_ms=100 output_allowed=0 uart_errors=0 rx_overflows=0 tx_timeouts=0 scheduler_overruns=0'
        if command == 'canstatus': return 'OK can1_started=1 can1_baud=500000 can1_start_error=0 can2_started=1 can2_baud=1000000 can2_start_error=0 output_allowed=0'
        if command == 'shoulder': return 'OK bus=shoulder started=1 start_error=0 rx=99 drop=0 invalid=0 fov=0 err=0 bus_off=0'
        if command == 'shoulderfeedback': return 'OK shoulder hb=1 state=1 error=00000000 fault=0 output_allowed=0 hb_ms=99'
        if command == 'outputstatus':
            f = dict(stage='S2_CAN_RX', s3_state=0, reason=0, configured=int(self.mode=='trial'),
                     transport_bound=int(self.mode=='trial'), output_allowed=0, request_valid=0,
                     arm_token=0, prep_state=0, feedback_pending=0, stop_attempted=0)
            if self.prepared:
                self.output_reads += 1
                f.update(s3_state=4 if self.output_reads==1 else 5, prep_state=1 if self.output_reads==1 else 4)
            if self.pulsed: f.update(s3_state=3, reason=4, prep_state=0, stop_attempted=1)
            return 'OK ' + ' '.join(k+'='+str(v) for k,v in f.items())
        if command == 'qualstatus':
            if not self.qual_started: return words('qualstatus', self.qual())
            self.qual_reads += 1
            if self.case == 'qualification_deadline': return words('qualstatus', self.qual(1))
            f = self.qual(3 if self.case=='qualification_fault' else 1 if self.qual_reads==1 else 2)
            if self.case == 'qualification_count' and f['state']==2: f['attempted'] = 8
            return words('qualstatus', f)
        if command == 'qualify':
            self.qual_started=True
            return 'OK qualification_started output_allowed=0'
        if command.startswith('qualfield '):
            index=int(command.split()[1]); endpoint, kind=self.m.FIELDS[index]
            value_bits = int(bits(.25 if index==0 else -.125 if index==4 else 0),16) if kind=='float' else 0 if index==2 else 1
            envelope=b'\x00'+struct.pack('<H',endpoint)+b'\x00'
            f=dict(field=index,ep=endpoint,requested=1,replied=1,decoded=1,q=101+2*index,r=102+2*index,
                   bits='%08X'%value_bits,req=(envelope+b'\x00'*4).hex().upper(),
                   raw=(envelope+struct.pack('<I',value_bits)).hex().upper(),id='025',dlc=8)
            return words('qualfield',f)
        if command == 'qualcancel': return 'OK qualification_cancelled output_allowed=0'
        if command == 'qualerror': return 'OK qualerror present=1 id=025 dlc=8 rx=110 raw=0000000000000000'
        if command in self.m.GROUPS: return words(command,self.plan[command])
        if command == 's3anchor':
            f = dict(coords=3, phase=0, pos=bits(0), seq=0, q=0, r=0, begin=0, now=100, out=0)
            if self.prepared:
                f.update(phase=3 if self.pulsed else 2, pos=bits(37.125), seq=1,
                         q=131, r=132, begin=130, now=250 if self.pulsed else 165)
            if self.case == 'relative_anchor_bad' and self.prepared and not self.pulsed:
                f['q'] = f['begin']  # A pre-event/same-ms read cannot supply the origin.
            if self.case == 'relative_anchor_reset' and self.pulsed:
                f.update(phase=0, pos=bits(0), seq=0, q=0, r=0, begin=0)
            if self.case == 'relative_anchor_changed' and self.pulsed:
                f['pos'] = bits(37.25)  # Retired phase alone cannot hide changed identity.
            return words('s3anchor', f)
        if command == 'prepare': self.prepared=True; return 'OK preparing output_allowed=0'
        if command == 's3properties':
            return 'OK s3properties seq=9 now=165 fresh=1 tq=00000000 cal=1 ready=1 cm=1 im=1 zq=154 mq=155 iq=156 first=150 last=160'
        if command == 'arm': return 'OK armed token=13'
        if command.startswith('shoulderpulse '): self.pulsed=True; return 'OK request'
        if command == 's3feedback':
            if not self.pulsed: return words('s3feedback',self.sample(10,165,150,0))
            self.feedback_reads += 1
            n=self.feedback_reads
            if self.case == 'short_ttl':
                f=(self.sample(10,210,150,0,0,0) if n==1 else
                   self.sample(11,223,220,.0002,1,0) if n==2 else self.sample(11,250,220,.0002,0,0))
            elif self.case == 'duplicate_ok':
                f=self.sample(11,183 if n==1 else 184,180,.0001,1,1) if n<=2 else self.sample(12,193,190,.0002,1,0)
            else:
                f=self.sample(11,183,180,.0001,1,1) if n==1 else self.sample(12,193,190,.0002,1,1) if n==2 else self.sample(12,220,190,.0002,0,0)
            if self.case == 'trial_invalid': f['pos']='7FC00000'
            if self.case == 'trial_stale': f['pq']=1
            if self.case == 'duplicate_changed' and n==2: f.update(seq=11,pq=180,pr=181,vq=182,vr=183,pos=bits(.0005))
            return words('s3feedback',f)
        if command == 's3reader':
            if not self.pulsed:
                return 'OK s3reader state=2 reason=0 requests=14 stop=0 observed=0 obs=0 hb=8 hms=160 error=00000000 fault=0 win=900 out=0 now=165 hvalid=1'
            self.reader_reads+=1
            complete=self.reader_reads>=2
            f=dict(state=7 if complete else 6,reason=0,requests=28 if complete else 21,
                   stop=210,observed=int(complete),obs=240 if complete else 0,hb=1,hms=241 if complete else 211,
                   error='00000000',fault=0,win=0 if complete else 99,out=0,now=250 if complete else 215,hvalid=int(not complete))
            if self.case == 'trial_unobserved' and complete: f['observed']=0
            if self.case == 'trial_reader_fault': f.update(state=4,reason=1)
            return words('s3reader',f)
        if command == 'stopstatus':
            return 'OK policy=2 source=shoulder_sdo seen=1 fresh=0 stationary=0 dwell_ms=0 age_ms=40 observed=1 reader=7 stop_accepted=1 purge_ok=1 stop_delivery_failed=0'
        if command == 'cancel': return 'OK cancelled output_allowed=0'
        raise AssertionError('Unexpected fake command '+command)


class FakeSerial:
    def __init__(self,machine,**kwargs):
        self.machine,self.is_open,self.reply=machine,False,b''
        self.dtr=self.rts=True
    def open(self):
        assert self.dtr is False and self.rts is False
        self.is_open=True
    def close(self): self.is_open=False
    def write(self,wire):
        command=wire.decode('ascii').rstrip('\n')
        self.machine.clock.value += .15 if self.machine.case=='qualification_deadline' else .002
        answer=self.machine.respond(command)
        self.reply=(answer+'\r\n').encode('ascii')
        return len(wire)
    def read_until(self,terminator,size):
        self.machine.clock.value += .002
        return self.reply[:size]


def run_case(m,name):
    mode='qualify' if name.startswith('qualification_') else 'trial'
    machine=Machine(m,mode,name)
    link=m.Link('OFFLINE_FAKE_PORT',machine.journal,mode,
                lambda **kw: FakeSerial(machine,**kw),5,100,machine.clock)
    summary=m.session(link,mode,machine.plan if mode=='trial' else None,machine.journal,machine.clock)
    pulses=[c for c in machine.commands if c.startswith('shoulderpulse ')]
    assert len(pulses)<=1 and machine.commands.count('arm')<=1 and machine.commands.count('prepare')<=1
    if name in ('qualification_success','trial_success','short_ttl','duplicate_ok','relative_success'):
        if not summary['success']:
            print(json.dumps({'counterexample':name,'summary':summary,'commands':machine.commands},allow_nan=False))
        assert summary['success'], summary.get('error')
        assert not summary['cancel_attempted']
        if mode=='qualify':
            assert len(summary['qualification_fields'])==7 and not pulses
            assert summary['qualification_fields'][2]['value'] is False
            assert summary['qualification_status']['cancelled']=='1'
        else:
            arm=machine.commands.index('arm')
            assert machine.commands[arm+1].startswith('shoulderpulse ')
            assert machine.commands[:arm].count('s3feedback')>=1
            assert summary['reader_final']['observed']=='1' and summary['last_output']['reason']=='4'
            assert summary['stop_status']['fresh']=='0' and summary['stop_status']['stationary']=='0'
            assert summary['motion_evidence']['distinct_pairs']>=2
            if name == 'relative_success':
                assert machine.commands.count('s3anchor') == 3
                before = machine.commands.index('s3anchor')
                ready = machine.commands.index('s3anchor', before + 1)
                final = machine.commands.index('s3anchor', ready + 1)
                assert before < machine.commands.index('prepare') < ready < arm < final
                assert summary['anchor_before_prepare']['phase'] == '0'
                assert summary['anchor_ready']['phase'] == '2' and summary['anchor_final']['phase'] == '3'
                assert summary['anchor_ready']['origin_rev'] == 37.125
                assert all(summary['anchor_ready'][key] == summary['anchor_final'][key]
                           for key in ('pos','seq','q','r','begin'))
    else:
        assert not summary['success'] and summary['cancel_attempted']
        cleanup='qualcancel' if mode=='qualify' else 'cancel'
        assert machine.commands.count(cleanup)==1
        if mode=='qualify': assert not pulses and not any(c in machine.commands for c in ('prepare','arm','cancel'))
        if name == 'relative_anchor_bad':
            assert not pulses and 'arm' not in machine.commands
        if name in ('relative_anchor_reset','relative_anchor_changed'):
            assert len(pulses) == 1 and summary['stop_status']['stop_accepted'] == '1'
            assert 'anchor_final' in summary and not summary.get('action_stop_success', False)
    if not name.startswith('relative_'):
        assert 's3anchor' not in machine.commands
    link.serial.close()
    assert link.serial.is_open is False


NAMES=('qualification_success','qualification_fault','qualification_count','qualification_deadline',
       'trial_success','short_ttl','duplicate_ok','duplicate_changed','trial_stale','trial_invalid',
       'trial_reader_fault','trial_unobserved','guards','relative_success','relative_anchor_bad',
       'relative_anchor_reset','relative_anchor_changed')


def guards(m):
    m.validate_plan(fixture())
    m.validate_plan(relative_fixture())
    assembled=fixture(); assembled['s3limits'].update(coords='2',direction='-1',ratio=bits(2))
    m.validate_plan(assembled)  # Existing coords1/2 mapping contracts remain compatible.
    equal=fixture(); equal['pulse']['signed_mNm']=20
    m.validate_plan(equal)  # Exact firmware f32 arithmetic accepts the configured limit.
    over=fixture(); over['pulse']['signed_mNm']=21
    short_stop=fixture(); short_stop['s3stopcfg']['window']='100'
    disabled=fixture(); disabled['approved']=False
    relative_bad=[]
    for key,value in (('ratio',bits(2)),('direction','-1'),('zero',bits(.25))):
        invalid=relative_fixture(); invalid['s3limits'][key]=value; relative_bad.append(invalid)
    for fn in (lambda:m.validate_plan(disabled),
               lambda:m.validate_plan(over),
               lambda:m.timing_budget(short_stop,13),
               lambda:m.feedback('OK s3feedback seq=1 seq=2'),
               lambda:m.feedback('OK s3feedback seq=1 now=2 fresh=0 pos=ZZZZZZZZ vel=00000000 pq=0 pr=0 vq=0 vr=0 out=0'),
               *(lambda plan=plan:m.validate_plan(plan) for plan in relative_bad),
               lambda:m.anchor('OK s3anchor coords=3 phase=2 pos=7FC00000 seq=1 q=2 r=3 begin=1 now=4 out=0'),
               lambda:m.anchor('OK s3anchor coords=3 phase=2 pos=00000000 seq=1 q=2 r=3 begin=1 now=4 out=0 extra=1')):
        try: fn()
        except Exception: pass
        else: raise AssertionError('Invalid plan/parser passed')
    machine=Machine(m,'qualify','guards')
    link=m.Link('OFFLINE_FAKE_PORT',machine.journal,'qualify',lambda **kw:FakeSerial(machine,**kw),5,1,machine.clock)
    try: link.query('arm')
    except m.CaptureError: pass
    else: raise AssertionError('Qualification allowed output')
    assert machine.commands==[]
    assert link.query('ping')=='OK pong'
    try: link.query('ping')
    except m.CaptureError: pass
    else: raise AssertionError('Query count ignored')
    assert machine.commands==['ping']
    link.serial.close()


def run_tests(m, selected=None):
    names=[selected] if selected else list(NAMES)
    assert not any(k=='serial' or k.startswith('serial.') for k in sys.modules)
    for name in names:
        (guards(m) if name=='guards' else run_case(m,name))
        print('PASS '+name)
    assert not any(k=='serial' or k.startswith('serial.') for k in sys.modules)
    print(json.dumps({'offline':'PASS','tests':names,'pass_count':len(names),
                      'hardware_access':False,'serial_imported':False}))
    return 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case',choices=NAMES)
    args=parser.parse_args()
    import capture
    raise SystemExit(run_tests(capture,args.case))
