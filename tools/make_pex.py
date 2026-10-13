"""Writes TES64's two Papyrus scripts (compiled, Skyrim .pex v3.2) without Bethesda's compiler:

  TES64_MCM    extends SKI_ConfigBase: the Mod Configuration Menu page, built generically from the plugin's own settings
               table (TES64_Native), so it needs SkyUI and nothing else.
  TES64_Native the plugin's native functions (SKSE registers them): the settings table and its values.

The Papyrus each function stands for is in the comments above it.
Usage: make_pex.py <out Scripts dir>"""
import os
import struct
import sys


class Pex:
    def __init__(self, source):
        self.source = source
        self.strings = []

    def s(self, text):
        if text not in self.strings:
            self.strings.append(text)
        return self.strings.index(text)


def ws(text):
    b = text.encode("latin1")
    return struct.pack(">H", len(b)) + b


class Func:
    """name, return type, params [(name, type)], locals, instructions; flags: 1 global, 2 native."""

    def __init__(self, name, ret="None", params=(), flags=0):
        self.name, self.ret, self.params, self.flags = name, ret, list(params), flags
        self.locals = {}
        self.code = []      # ("op", opcode, args) | ("label", name, ())

    def local(self, name, typ):
        self.locals[name] = typ

    def emit(self, op, *args):
        self.code.append(("op", op, args))

    def label(self, name):
        self.code.append(("label", name, ()))


def I(n): return ("id", n)
def S(t): return ("str", t)
def N(n): return ("int", n)
def B(b): return ("bool", b)
def L(name): return ("label", name)
NULL = ("null", None)


VARARG = {23: 3, 24: 2, 25: 3}   # callmethod, callparent, callstatic: fixed args, then a count, then the arguments


def encode_func(px, fn):
    out = struct.pack(">HHIB", px.s(fn.ret), px.s(""), 0, fn.flags)
    out += struct.pack(">H", len(fn.params))
    for n, t in fn.params:
        out += struct.pack(">HH", px.s(n), px.s(t))
    out += struct.pack(">H", len(fn.locals))
    for n, t in fn.locals.items():
        out += struct.pack(">HH", px.s(n), px.s(t))
    code = list(fn.code)
    if fn.ret == "None" and not (fn.flags & 2):
        code.append(("op", 26, (NULL,)))
    index, ops = {}, []
    for kind, a, args in code:
        if kind == "label":
            index[a] = len(ops)
        else:
            ops.append((a, args))
    out += struct.pack(">H", len(ops))
    for i, (op, args) in enumerate(ops):
        out += struct.pack(">B", op)
        seq = list(args)
        fixed = VARARG.get(op)
        if fixed is not None:
            seq = seq[:fixed] + [N(len(seq) - fixed)] + seq[fixed:]
        for t, v in seq:
            if t == "label":
                t, v = "int", index[v] - i       # jumps are relative to the instruction itself
            if t == "null":
                out += struct.pack(">B", 0)
            elif t == "id":
                out += struct.pack(">BH", 1, px.s(v))
            elif t == "str":
                out += struct.pack(">BH", 2, px.s(v))
            elif t == "int":
                out += struct.pack(">Bi", 3, v)
            elif t == "bool":
                out += struct.pack(">BB", 5, 1 if v else 0)
    return out


def write(px, name, parent, variables, funcs, path):
    """variables [(name, type)], funcs [Func] (all in the default state)."""
    px.s("")
    body = struct.pack(">HHIH", px.s(parent), px.s(""), 0, px.s(""))
    body += struct.pack(">H", len(variables))
    for n, t in variables:
        body += struct.pack(">HHIB", px.s(n), px.s(t), 0, 0)
    body += struct.pack(">H", 0)                 # properties
    body += struct.pack(">H", 1)                 # states: the default one
    body += struct.pack(">HH", px.s(""), len(funcs))
    for fn in funcs:
        body += struct.pack(">H", px.s(fn.name)) + encode_func(px, fn)
    obj = struct.pack(">H", px.s(name)) + struct.pack(">I", 4 + len(body)) + body
    head = struct.pack(">IBBHQ", 0xFA57C0DE, 3, 2, 1, 0x65000000) + ws(px.source) + ws("tes64") + ws("tes64")
    table = struct.pack(">H", len(px.strings)) + b"".join(ws(t) for t in px.strings)
    out = head + table + struct.pack(">B", 0) + struct.pack(">H", 0) + struct.pack(">H", 1) + obj
    with open(path, "wb") as f:
        f.write(out)


# ---------------------------------------------------------------------------------------------------------------------
# TES64_Native: Scriptname TES64_Native / Int Function Count() global native / ...   (SKSE registers the bodies)
NATIVES = [
    ("Count", "Int", []), ("Kind", "Int", [("i", "Int")]), ("Label", "String", [("i", "Int")]), ("Help", "String", [("i", "Int")]),
    ("Format", "String", [("i", "Int")]), ("GetBool", "Bool", [("i", "Int")]), ("GetFloat", "Float", [("i", "Int")]),
    ("GetInt", "Int", [("i", "Int")]), ("SetBool", "None", [("i", "Int"), ("v", "Bool")]),
    ("SetFloat", "None", [("i", "Int"), ("v", "Float")]), ("SetInt", "None", [("i", "Int"), ("v", "Int")]),
    ("Min", "Float", [("i", "Int")]), ("Max", "Float", [("i", "Int")]), ("Step", "Float", [("i", "Int")]),
    ("Default", "Float", [("i", "Int")]), ("Reset", "None", [("i", "Int")]),
]


def native_script(path):
    px = Pex("TES64_Native.psc")
    write(px, "TES64_Native", "", [], [Func(n, r, p, flags=3) for n, r, p in NATIVES], path)


# ---------------------------------------------------------------------------------------------------------------------
# TES64_MCM:  Scriptname TES64_MCM extends SKI_ConfigBase  /  Int[] opts
IADD, NOT, ASSIGN, CMP_EQ, CMP_LT, CMP_GE, JMP, JMPF, CALLMETHOD, CALLSTATIC, RET, PROPSET, ARR_CREATE, ARR_GET, ARR_SET = (
    1, 10, 13, 15, 16, 19, 20, 22, 23, 25, 26, 29, 30, 32, 33)
NONE = "::NoneVar"


def call(fn, name, dest, *args):          # self.name(args)
    fn.emit(CALLMETHOD, I(name), I("self"), I(dest), *args)


def native(fn, name, dest, *args):        # TES64_Native.name(args)
    fn.emit(CALLSTATIC, I("TES64_Native"), I(name), I(dest), *args)


def mcm_script(path):
    px = Pex("TES64_MCM.psc")
    funcs = []

    # Event OnConfigInit()
    #     ModName = "TES64 - Mario"
    #     Pages = new String[1]
    #     Pages[0] = "Mario"
    f = Func("OnConfigInit")
    f.local(NONE, "None")
    f.local("pages", "String[]")
    f.emit(PROPSET, I("ModName"), I("self"), S("TES64 - Mario"))
    f.emit(ARR_CREATE, I("pages"), N(1))
    f.emit(ARR_SET, I("pages"), N(0), S("Mario"))
    f.emit(PROPSET, I("Pages"), I("self"), I("pages"))
    funcs.append(f)

    # Int Function IndexOf(Int a_option)           ; which table entry an option belongs to (-1: none)
    #     Int want = a_option + 1
    #     Int i = 0
    #     While i < 64
    #         If opts[i] == want
    #             Return i
    #         EndIf
    #         i += 1
    #     EndWhile
    #     Return -1
    f = Func("IndexOf", "Int", [("a_option", "Int")])
    for n, t in (("i", "Int"), ("t", "Bool"), ("id", "Int"), ("want", "Int")):
        f.local(n, t)
    f.emit(IADD, I("want"), I("a_option"), N(1))
    f.emit(ASSIGN, I("i"), N(0))
    f.label("top")
    f.emit(CMP_LT, I("t"), I("i"), N(64))
    f.emit(JMPF, I("t"), L("end"))
    f.emit(ARR_GET, I("id"), I("opts"), I("i"))
    f.emit(CMP_EQ, I("t"), I("id"), I("want"))
    f.emit(JMPF, I("t"), L("next"))
    f.emit(26, I("i"))
    f.label("next")
    f.emit(IADD, I("i"), I("i"), N(1))
    f.emit(JMP, L("top"))
    f.label("end")
    f.emit(26, N(-1))
    funcs.append(f)

    # Event OnPageReset(String a_page)
    #     SetCursorFillMode(1)                          ; TOP_TO_BOTTOM
    #     opts = new Int[64]
    #     Int n = TES64_Native.Count()
    #     Int i = 0
    #     While i < n
    #         Int kind = TES64_Native.Kind(i)
    #         String label = TES64_Native.Label(i)
    #         Int id = 0
    #         If kind == 0
    #             id = AddHeaderOption(label, 0)
    #         ElseIf kind == 1
    #             id = AddToggleOption(label, TES64_Native.GetBool(i), 0)
    #         ElseIf kind == 2
    #             id = AddSliderOption(label, TES64_Native.GetFloat(i), TES64_Native.Format(i), 0)
    #         ElseIf kind == 3
    #             id = AddKeyMapOption(label, TES64_Native.GetInt(i), 0)
    #         EndIf
    #         opts[i] = id + 1
    #         i += 1
    #     EndWhile
    # EndEvent
    f = Func("OnPageReset", "None", [("a_page", "String")])
    for n, t in (("n", "Int"), ("i", "Int"), ("kind", "Int"), ("label", "String"), ("id", "Int"), ("t", "Bool"), ("b", "Bool"),
                 ("fv", "Float"), ("iv", "Int"), ("fmt", "String"), ("arr", "Int[]"), (NONE, "None")):
        f.local(n, t)
    call(f, "SetCursorFillMode", NONE, N(1))
    f.emit(ARR_CREATE, I("arr"), N(64))
    f.emit(ASSIGN, I("opts"), I("arr"))
    native(f, "Count", "n")
    f.emit(ASSIGN, I("i"), N(0))
    f.label("top")
    f.emit(CMP_LT, I("t"), I("i"), I("n"))
    f.emit(JMPF, I("t"), L("end"))
    native(f, "Kind", "kind", I("i"))
    native(f, "Label", "label", I("i"))
    f.emit(ASSIGN, I("id"), N(0))
    f.emit(CMP_EQ, I("t"), I("kind"), N(0))
    f.emit(JMPF, I("t"), L("k1"))
    call(f, "AddHeaderOption", "id", I("label"), N(0))
    f.emit(JMP, L("stored"))
    f.label("k1")
    f.emit(CMP_EQ, I("t"), I("kind"), N(1))
    f.emit(JMPF, I("t"), L("k2"))
    native(f, "GetBool", "b", I("i"))
    call(f, "AddToggleOption", "id", I("label"), I("b"), N(0))
    f.emit(JMP, L("stored"))
    f.label("k2")
    f.emit(CMP_EQ, I("t"), I("kind"), N(2))
    f.emit(JMPF, I("t"), L("k3"))
    native(f, "GetFloat", "fv", I("i"))
    native(f, "Format", "fmt", I("i"))
    call(f, "AddSliderOption", "id", I("label"), I("fv"), I("fmt"), N(0))
    f.emit(JMP, L("stored"))
    f.label("k3")
    f.emit(CMP_EQ, I("t"), I("kind"), N(3))
    f.emit(JMPF, I("t"), L("stored"))
    native(f, "GetInt", "iv", I("i"))
    call(f, "AddKeyMapOption", "id", I("label"), I("iv"), N(0))
    f.label("stored")
    f.emit(IADD, I("id"), I("id"), N(1))
    f.emit(ARR_SET, I("opts"), I("i"), I("id"))
    f.emit(IADD, I("i"), I("i"), N(1))
    f.emit(JMP, L("top"))
    f.label("end")
    funcs.append(f)

    def with_index(name, params, body):
        """Event name(params) / Int i = IndexOf(a_option) / If i >= 0 / body / EndIf"""
        g = Func(name, "None", params)
        for n, t in (("i", "Int"), ("t", "Bool"), ("kind", "Int"), ("b", "Bool"), ("fv", "Float"), ("fmt", "String"),
                     ("s", "String"), ("lo", "Float"), ("hi", "Float"), (NONE, "None")):
            g.local(n, t)
        call(g, "IndexOf", "i", I("a_option"))
        g.emit(CMP_GE, I("t"), I("i"), N(0))
        g.emit(JMPF, I("t"), L("end"))
        body(g)
        g.label("end")
        funcs.append(g)

    # Event OnOptionSelect(Int a_option):  If TES64_Native.Kind(i) == 1
    #     Bool b = !TES64_Native.GetBool(i)
    #     TES64_Native.SetBool(i, b)
    #     SetToggleOptionValue(a_option, b, False)
    def select(g):
        native(g, "Kind", "kind", I("i"))
        g.emit(CMP_EQ, I("t"), I("kind"), N(1))
        g.emit(JMPF, I("t"), L("end"))
        native(g, "GetBool", "b", I("i"))
        g.emit(NOT, I("b"), I("b"))
        native(g, "SetBool", NONE, I("i"), I("b"))
        call(g, "SetToggleOptionValue", NONE, I("a_option"), I("b"), B(False))
    with_index("OnOptionSelect", [("a_option", "Int")], select)

    # Event OnOptionSliderOpen(Int a_option)
    #     SetSliderDialogStartValue(TES64_Native.GetFloat(i))
    #     SetSliderDialogDefaultValue(TES64_Native.Default(i))
    #     SetSliderDialogRange(TES64_Native.Min(i), TES64_Native.Max(i))
    #     SetSliderDialogInterval(TES64_Native.Step(i))
    def slider_open(g):
        native(g, "GetFloat", "fv", I("i"))
        call(g, "SetSliderDialogStartValue", NONE, I("fv"))
        native(g, "Default", "fv", I("i"))
        call(g, "SetSliderDialogDefaultValue", NONE, I("fv"))
        native(g, "Min", "lo", I("i"))
        native(g, "Max", "hi", I("i"))
        call(g, "SetSliderDialogRange", NONE, I("lo"), I("hi"))
        native(g, "Step", "fv", I("i"))
        call(g, "SetSliderDialogInterval", NONE, I("fv"))
    with_index("OnOptionSliderOpen", [("a_option", "Int")], slider_open)

    # Event OnOptionSliderAccept(Int a_option, Float a_value)
    #     TES64_Native.SetFloat(i, a_value)
    #     SetSliderOptionValue(a_option, a_value, TES64_Native.Format(i), False)
    def slider_accept(g):
        native(g, "SetFloat", NONE, I("i"), I("a_value"))
        native(g, "Format", "fmt", I("i"))
        call(g, "SetSliderOptionValue", NONE, I("a_option"), I("a_value"), I("fmt"), B(False))
    with_index("OnOptionSliderAccept", [("a_option", "Int"), ("a_value", "Float")], slider_accept)

    # Event OnOptionKeyMapChange(Int a_option, Int a_keyCode, String a_conflictControl, String a_conflictName)
    #     TES64_Native.SetInt(i, a_keyCode)
    #     SetKeyMapOptionValue(a_option, a_keyCode, False)
    def keymap(g):
        native(g, "SetInt", NONE, I("i"), I("a_keyCode"))
        call(g, "SetKeyMapOptionValue", NONE, I("a_option"), I("a_keyCode"), B(False))
    with_index("OnOptionKeyMapChange", [("a_option", "Int"), ("a_keyCode", "Int"), ("a_conflictControl", "String"),
                                       ("a_conflictName", "String")], keymap)

    # Event OnOptionDefault(Int a_option)
    #     TES64_Native.Reset(i)
    #     ForcePageReset()
    def default(g):
        native(g, "Reset", NONE, I("i"))
        call(g, "ForcePageReset", NONE)
    with_index("OnOptionDefault", [("a_option", "Int")], default)

    # Event OnOptionHighlight(Int a_option)
    #     SetInfoText(TES64_Native.Help(i))
    def highlight(g):
        native(g, "Help", "s", I("i"))
        call(g, "SetInfoText", NONE, I("s"))
    with_index("OnOptionHighlight", [("a_option", "Int")], highlight)

    write(px, "TES64_MCM", "SKI_ConfigBase", [("opts", "Int[]")], funcs, path)


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    native_script(os.path.join(out, "TES64_Native.pex"))
    mcm_script(os.path.join(out, "TES64_MCM.pex"))
    print("wrote", out)
