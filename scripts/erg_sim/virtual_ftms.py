"""Compile production FTMS queue processing against an in-memory GATT sink.

This boundary receives real control-point bytes and captures production replies.
It does not emulate ATT, subscriptions, connection ownership, or radio delivery.
Only well-formed request-control, start, stop/pause and target-power packets are
accepted by the adapter. It makes no claim about firmware malformed-write safety.
"""
from pathlib import Path

def prepare(folder, source_root):
    """Return declaration/implementation fragments for one firmware revision.

    Runtime/config and spinBLEClient must be declared before implementation;
    the latter needs connectedPM, connectedCD and FTMSControlPointWrite().
    spinBLEServer must supply spinDownFlag and a std::queue<std::string> writeCache.
    folder is accepted for parity with the sensor transport adapter.
    """
    from .bridge import function

    del folder
    root = Path(source_root)
    declarations = Path(__file__).with_suffix('.hpp').read_text(encoding='utf-8')
    source = (root / 'src/BLE_Fitness_Machine_Service.cpp').read_text(encoding='utf-8')
    processor = function(source, 'void BLE_Fitness_Machine_Service::processFTMSWrite(')
    # Scope substitutions strictly to peripheral logging/notification plumbing.
    # No production statement, branch, packet parsing or state write is changed.
    implementation = '''
#pragma push_macro("DirConManager")
#pragma push_macro("ss2k_log_hex_to_buffer")
#pragma push_macro("FMTS_SERVER_LOG_TAG")
#define DirConManager VirtualFtmsDirCon
#define ss2k_log_hex_to_buffer virtualFtmsLogHex
#define FMTS_SERVER_LOG_TAG "VirtualFTMS"
''' + processor + '''
#pragma pop_macro("FMTS_SERVER_LOG_TAG")
#pragma pop_macro("ss2k_log_hex_to_buffer")
#pragma pop_macro("DirConManager")
'''
    return declarations, implementation
