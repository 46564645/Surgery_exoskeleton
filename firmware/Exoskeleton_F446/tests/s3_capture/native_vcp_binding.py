"""Read-only active VCP identity using Windows SDK Configuration Manager APIs."""
import re
import time

PARENT = r'USB\VID_0483&PID_374B\066EFF555748874987182240'
PREFIX = r'USB\VID_0483&PID_374B&MI_02' + chr(92)


def binding_probe():
    # Imports and device reads are live-only. SDK cfgmgr32.h/cfg.h define these APIs.
    import ctypes as c
    import os
    import winreg as reg
    from pathlib import Path
    started = time.monotonic()
    api = c.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32/cfgmgr32.dll'))
    u32 = c.c_uint32
    ptr = c.POINTER(u32)
    api.CM_Locate_DevNodeW.argtypes = [ptr, c.c_wchar_p, u32]
    api.CM_Get_DevNode_Status.argtypes = [ptr, ptr, u32, u32]
    api.CM_Get_Parent.argtypes = [ptr, u32, u32]
    api.CM_Get_Device_IDW.argtypes = [u32, c.c_wchar_p, u32, u32]
    for name in ('CM_Locate_DevNodeW', 'CM_Get_DevNode_Status', 'CM_Get_Parent', 'CM_Get_Device_IDW'):
        getattr(api, name).restype = u32

    def require(condition, message):
        if not condition:
            raise RuntimeError(message)

    def enum(handle, values=False):
        count = reg.QueryInfoKey(handle)[1 if values else 0]
        return [reg.EnumValue(handle, i) if values else reg.EnumKey(handle, i) for i in range(count)]

    def pass_once():
        with reg.OpenKey(reg.HKEY_LOCAL_MACHINE, r'HARDWARE\DEVICEMAP\SERIALCOMM') as h:
            active = {str(value): name for name, value, kind in enum(h, True)}
        base = r'SYSTEM\CurrentControlSet\Enum' + chr(92) + PREFIX.rstrip(chr(92))
        with reg.OpenKey(reg.HKEY_LOCAL_MACHINE, base) as h:
            instances = enum(h)
        ports = []
        for instance in instances:
            device_id = PREFIX + instance
            node = u32()
            if api.CM_Locate_DevNodeW(c.byref(node), device_id, 0) != 0:
                continue  # NORMAL requires an active devnode; no phantom fallback.
            status, problem, parent_node = u32(), u32(), u32()
            require(api.CM_Get_DevNode_Status(c.byref(status), c.byref(problem), node, 0) == 0, 'CM status read failed')
            require(status.value & 8 and not status.value & 0x400 and problem.value == 0, 'Active VCP is unhealthy')
            require(api.CM_Get_Parent(c.byref(parent_node), node, 0) == 0, 'CM parent read failed')
            parent_id = c.create_unicode_buffer(512)
            require(api.CM_Get_Device_IDW(parent_node, parent_id, len(parent_id), 0) == 0, 'CM parent ID read failed')
            require(parent_id.value == PARENT, 'Active ST-LINK has unexpected serial')
            with reg.OpenKey(reg.HKEY_LOCAL_MACHINE, base + '\\' + instance + r'\Device Parameters') as h:
                port = reg.QueryValueEx(h, 'PortName')[0]
            require(re.fullmatch(r'COM[0-9]+', port) and port in active, 'VCP port is absent from active device map')
            ports.append({'DeviceID': port, 'PNPDeviceID': device_id, 'Status': 'OK',
                'ConfigManagerErrorCode': problem.value, 'devnode_status': status.value,
                'native_device': active[port]})
        require(len(ports) == 1, 'No unique active healthy fixed-serial VCP')
        return ports[0]

    first, second = pass_once(), pass_once()
    require(first == second, 'Device identity changed between native reads')
    return {'ports': [second], 'parent': PARENT, 'serial_verified': True,
        'read_only_enumeration': True, 'source': 'registry_and_cfgmgr32_active_devnode',
        'elapsed_seconds': time.monotonic() - started}


if __name__ == '__main__':
    import argparse
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', action='store_true', help='Read the live fixed VCP identity; never opens its serial port')
    if parser.parse_args().probe:
        print(json.dumps(binding_probe()))
