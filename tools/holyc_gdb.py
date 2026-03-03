"""
HolyC GDB Python pretty-printers.

Load with:
    source /path/to/tools/holyc_gdb.py

Or add to ~/.gdbinit:
    source /path/to/tools/holyc_gdb.py

These printers make HolyC runtime types readable in GDB sessions
when debugging programs compiled with `hcc -g`.
"""
import gdb
import gdb.printing


class HolyCValuePrinter:
    """Pretty-printer for HolyC Value struct (holyc::Value)."""

    # Value::Kind enum का order interpreter/Interpreter.h से match करना चाहिए
    KIND_NAMES = {
        0: 'Void',
        1: 'Int',
        2: 'Float',
        3: 'Ptr',
        4: 'FuncPtr',
    }

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            kind = int(self.val['kind'])
            kind_name = self.KIND_NAMES.get(kind, 'Unknown({})'.format(kind))
            if kind == 1:  # Int है
                return 'HolyC::Value({}, {})'.format(kind_name, int(self.val['i']))
            elif kind == 2:  # Float है
                return 'HolyC::Value({}, {})'.format(kind_name, float(self.val['f']))
            elif kind == 3:  # Ptr है
                return 'HolyC::Value({}, {})'.format(kind_name, self.val['ptr'])
            else:
                return 'HolyC::Value({})'.format(kind_name)
        except Exception as e:
            return 'HolyC::Value(<error: {}>)'.format(e)

    def display_hint(self):
        return 'string'


class HolyCCallFramePrinter:
    """Pretty-printer for HolyC CallFrame struct."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            func_name = self.val['func_name']
            return 'HolyC::CallFrame(func={})'.format(func_name)
        except Exception as e:
            return 'HolyC::CallFrame(<error: {}>)'.format(e)

    def children(self):
        try:
            vars_map = self.val['vars']
            # std::unordered_map की entries को iterate करो अगर हो सके
            result = []
            try:
                for entry in vars_map:
                    result.append((str(entry['first']), entry['second']))
            except Exception:
                pass
            return iter(result)
        except Exception:
            return iter([])

    def display_hint(self):
        return 'map'


class HolyCTypePrinter:
    """Pretty-printer for HolyC Type struct."""

    KIND_NAMES = {
        0: 'Void',
        1: 'Bool',
        2: 'Int',
        3: 'UInt',
        4: 'Float',
        5: 'Pointer',
        6: 'Array',
        7: 'Struct',
        8: 'Function',
    }

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            kind = int(self.val['kind'])
            kind_name = self.KIND_NAMES.get(kind, 'Unknown({})'.format(kind))
            if kind in (2, 3):  # Int या UInt है
                bits = int(self.val['bits'])
                return 'HolyC::Type({}{})'.format('U' if kind == 3 else 'I', bits)
            elif kind == 4:  # Float है
                bits = int(self.val['bits'])
                return 'HolyC::Type(F{})'.format(bits)
            elif kind == 5:  # Pointer है
                stars = int(self.val['stars'])
                return 'HolyC::Type(Pointer, stars={})'.format(stars)
            else:
                return 'HolyC::Type({})'.format(kind_name)
        except Exception as e:
            return 'HolyC::Type(<error: {}>)'.format(e)

    def display_hint(self):
        return 'string'


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter('holyc')
    # bare names और fully-qualified names दोनों को match करो
    pp.add_printer('Value',     r'^(holyc::)?Value$',     HolyCValuePrinter)
    pp.add_printer('CallFrame', r'^(holyc::)?CallFrame$', HolyCCallFramePrinter)
    pp.add_printer('Type',      r'^(holyc::)?Type$',      HolyCTypePrinter)
    return pp


# pretty printers को register करो
try:
    gdb.printing.register_pretty_printer(
        gdb.current_objfile(),
        build_pretty_printer(),
        replace=True,
    )
    print("HolyC GDB pretty-printers loaded.")
except Exception:
    # GDB के बाहर चल रहा है (जैसे import testing में GDB के बिना)
    pass
