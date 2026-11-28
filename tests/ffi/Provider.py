"""Compare the native ABI v1 method with the independent C oracle in one disposable process."""
import argparse
import ctypes as C
import errno
import json
import math
import os
from pathlib import Path
import statistics
import struct
import subprocess
import time


class Value(C.Structure):
    _fields_ = [("StructSize", C.c_uint32), ("Type", C.c_uint32), ("Number", C.c_double),
                ("Text", C.c_void_p), ("TextLength", C.c_uint64), ("Boolean", C.c_int32), ("Reserved", C.c_uint32)]


class Info(C.Structure):
    _fields_ = [("StructSize", C.c_uint32), ("AbiVersion", C.c_uint32), ("Name", C.c_char_p),
                ("ExtensionVersion", C.c_uint32), ("RequiredCapabilities", C.c_uint64)]


Method = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Value), C.c_uint32, C.POINTER(Value))
Register = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.c_char_p, C.c_char_p, Method, C.c_void_p)
Compute = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                      C.POINTER(C.c_size_t))


class Host(C.Structure):
    _fields_ = [("StructSize", C.c_uint32), ("AbiVersion", C.c_uint32), ("Context", C.c_void_p),
                ("Register", Register), ("Log", C.c_void_p), ("SetError", C.c_void_p),
                ("ScheduleUi", C.c_void_p), ("RegisterSignal", C.c_void_p), ("EmitSignal", C.c_void_p)]


class Session:
    def __init__(self, Provider, Fixture):
        self.Provider = C.CDLL(str(Provider))
        Query = self.Provider.LuiExtensionQuery
        Query.argtypes = [C.POINTER(Info)]
        Metadata = Info(C.sizeof(Info))
        assert Query(C.byref(Metadata)) and (Metadata.AbiVersion, Metadata.Name, Metadata.RequiredCapabilities) == (1, b"Ffi", 3)
        def Capture(_, Service, Name, Signature, Function, Context):
            if (Service, Name, Signature) != (b"Ffi", b"Dispatch", b"(Packet: string) -> string"):
                return 0
            self.Method, self.MethodContext = Function, Context
            return 1
        self.Register = Register(Capture)
        self.Host = Host(C.sizeof(Host), 1, None, self.Register)
        self.Context = C.c_void_p()
        Init = self.Provider.LuiExtensionInit
        Init.argtypes = [C.POINTER(Host), C.POINTER(C.c_void_p)]
        assert Init(C.byref(self.Host), C.byref(self.Context))
        self.Provider.LuiExtensionShutdown.argtypes = [C.c_void_p]
        self.Provider.LuiExtensionShutdown.restype = None
        self.Image = self.Dispatch("Open", Kind="Selected", Name=str(Fixture))["Ref"]

    def Raw(self, Operation, **Fields):
        Packet = json.dumps({"Op": Operation, **Fields}, separators=(",", ":"), allow_nan=False).encode()
        return json.loads(self.Exchange(Packet))

    def Exchange(self, Packet):
        In = Value(C.sizeof(Value), 3, 0, C.cast(C.c_char_p(Packet), C.c_void_p), len(Packet))
        Out = Value(C.sizeof(Value))
        assert self.Method(self.MethodContext, C.byref(In), 1, C.byref(Out)) and Out.Type == 3
        return C.string_at(Out.Text, Out.TextLength)

    def Dispatch(self, Operation, **Fields):
        Response = self.Raw(Operation, **Fields)
        assert Response["Ok"], (Operation, Response)
        return Response["Result"]

    def Error(self, Code, Operation, **Fields):
        assert self.Raw(Operation, **Fields) == {"Ok": False, "Error": Code}, (Operation, Code)

    def Bind(self, Name, Params, Result):
        return self.Dispatch("Bind", Library=self.Image, Symbol="LuiFfi" + Name, Params=Params, Result=Result)["Ref"]

    def Call(self, Function, *Args):
        return self.Dispatch("Call", Function=Function, Args=Args)["Value"]

    def Close(self):
        if self.Context:
            self.Provider.LuiExtensionShutdown(self.Context)
            self.Context = None


def Scalars(S, Oracle):
    Platform = S.Dispatch("Platform")
    assert (Platform["PointerBytes"], Platform["LongBytes"]) == (Oracle["PointerBytes"], Oracle["LongBytes"])
    for Name, Type, Cases in [
        ("I8", "i8", [-128, 127]), ("U8", "u8", [0, 255]),
        ("I16", "i16", [-32768, 32767]), ("U16", "u16", [0, 65535]),
        ("I32", "i32", [-2147483648, 2147483647]), ("U32", "u32", [0, 4294967295]),
        ("I64", "i64", ["8000000000000000", "7fffffffffffffff", "ffdfffffffffffff"]),
        ("U64", "u64", [Oracle["Unsigned64Hex"], "0020000000000001"]),
        ("ISize", "intptr", ["8000000000000000"]), ("USize", "uintptr", ["ffffffffffffffff"]),
        ("Long", "long", ["8000000000000000"] if Platform["LongBytes"] == 8 else [-2147483648]),
        ("F32", "f32", [-1.25, -0.0]), ("F64", "f64", [0.125, -0.0]),
    ]:
        F = S.Bind("Echo" + Name, [Type], Type)
        for Case in Cases:
            Answer = S.Call(F, Case)
            assert Answer == Case
            if isinstance(Case, float):
                assert math.copysign(1, Case) == math.copysign(1, Answer)
        assert S.Dispatch("Close", Ref=F)
    Bool = S.Bind("Bool8", ["bool8"], "bool8")
    assert S.Call(Bool, False) is False and S.Call(Bool, True) is True
    Mix = S.Bind("Mix", ["i8", "f64", "u16", "f32", "i32", "f64", "u32", "f32"], "f64")
    assert S.Call(Mix, -7, 1.5, 65000, -2.25, -3000, .125, 123456, 4) == Oracle["Mixed"]
    Params = ["i32", "i32", "f64", "i32", "f64", "u64", "utf8"]
    Options = {"Library": S.Image, "Symbol": "LuiFfiVariadicMix", "Result": "f64"}
    Variadic = S.Dispatch("BindVariadic", **Options, Params=Params, Fixed=1)["Ref"]
    assert S.Call(Variadic, 10, -7, 1.5, 65000, -2.25, "ffffffffffffffff", "A") == 65322.25
    S.Error("InvalidFixedCount", "BindVariadic", **Options, Params=Params, Fixed=0)
    S.Error("VariadicPromotionRequired", "BindVariadic", **Options, Params=["i32", "f32"], Fixed=1)
    S.Error("VariadicPromotionRequired", "BindVariadic", **Options, Params=["i32", "u16"], Fixed=1)
    assert S.Call(S.Bind("Text", [], "utf8")) == "caf\u00e9"


def Field(Name, Type):
    return {"Name": Name, "Type": Type}


def BinaryAndPacked(S, Native):
    def Packet(Op, Handle, Body=b""):
        return S.Exchange(b"LFB1" + Op + struct.pack("<I", Handle) + Body)

    def Number(Handle, Value, Op=b"C"):
        return Packet(Op, Handle, b"N" + struct.pack("<d", Value))

    U32 = S.Bind("EchoU32", ["u32"], "u32")
    F64 = S.Bind("EchoF64", ["f64"], "f64")
    Bool = S.Bind("Bool8", ["bool8"], "bool8")
    Word = S.Bind("EchoU64", ["u64"], "u64")
    Errors = S.Bind("SetErrors", ["i32"], "i32")
    assert Number(U32, 4294967295) == struct.pack("<Bcd", 0, b"N", 4294967295)
    assert Number(U32, 4294967295, b"E") == struct.pack("<BiIcd", 0, 0, 0, b"N", 4294967295)
    assert Number(F64, -0.0) == struct.pack("<Bcd", 0, b"N", -0.0)
    assert Packet(b"C", Bool, b"B" + struct.pack("<d", 1)) == b"\0T"
    assert Packet(b"E", Bool, b"B" + struct.pack("<d", 0)) == b"\0" * 9 + b"F"
    assert Number(Errors, -17, b"E") == struct.pack("<BiIcd", 0, errno.EDOM, 1234 if os.name == "nt" else 0, b"N", -17)
    assert Number(Errors, -17) == struct.pack("<Bcd", 0, b"N", -17)
    assert Number(U32, -1) == b"\1IntegerRange"
    assert Number(F64, float("inf")) == b"\1FloatRange"
    assert Number(Bool, 1) == b"\1ExpectedBoolean"
    assert Number(Word, 1) == b"\1ScalarSignatureRequired"
    assert Packet(b"H", Word, struct.pack("<Q", 0xfedcba9876543210)) == struct.pack("<BQ", 0, 0xfedcba9876543210)
    assert Packet(b"J", Word, struct.pack("<Q", 2**64-1)) == struct.pack("<BiIQ", 0, 0, 0, 2**64-1)
    assert Packet(b"H", Word, b"\0"*7) == b"\1ArgumentCount"
    assert Packet(b"C", U32) == b"\1ArgumentCount"
    assert S.Exchange(b"LFB1C") == b"\1InvalidBinaryPacket"
    assert S.Dispatch("Close", Ref=U32)
    assert Number(U32, 1) == b"\1Closed"

    Native.LuiFfiLayout.argtypes = [C.c_uint32]
    Native.LuiFfiLayout.restype = C.c_uint32
    Packed = S.Dispatch("Layout", Pack=2, Fields=[Field("Tag", "u8"), Field("Count", "u32"), Field("Tail", "u8")])
    assert [Packed["Size"], Packed["Alignment"], *Packed["Offsets"][1:]] == [Native.LuiFfiLayout(I) for I in range(11, 15)]
    Rows = S.Dispatch("Layout", Element=Packed["Ref"], Count=2)
    assert Rows["Offsets"] == [0, Packed["Size"]]
    Container = S.Dispatch("Layout", Fields=[Field("Header", "u8"), Field("Rows", Rows["Ref"]), Field("Trailer", "u32")])
    assert [Container["Size"], Container["Alignment"], *Container["Offsets"][1:]] == [Native.LuiFfiLayout(I) for I in range(15, 19)]
    Large = S.Dispatch("Layout", Pack=1, Fields=[Field("Tag", "u8"), Field("Value", "u64"), Field("Flags", "u16")])
    assert [Large["Size"], Large["Alignment"], *Large["Offsets"]] == [11, 1, 0, 1, 9]
    if os.name == "nt":
        Shifted = S.Bind("AdjustPacked", [Packed["Ref"], "u32"], Packed["Ref"])
        assert Packet(b"A", Shifted, struct.pack("<dddd", 1, 250, 3, 7)) == struct.pack("<Bddd", 0, 1, 257, 3)
        ShiftedLarge = S.Bind("AdjustPackedLarge", [Large["Ref"], "u32"], Large["Ref"])
        assert S.Call(ShiftedLarge, {"Tag": 2, "Value": "123456789abcdef0", "Flags": 7}, 2) == {
            "Tag": 2, "Value": "123456789abcdef2", "Flags": 0x52}
        assert Packet(b"H", ShiftedLarge, struct.pack("<dQdd", 2, 0x123456789abcdef0, 7, 2)) == struct.pack("<BdQd", 0, 2, 0x123456789abcdef2, 0x52)
        Callback = S.Dispatch("Callback", Params=[Packed["Ref"], Large["Ref"]], Result="i32", Fallback=17)
        CallPacked = S.Bind("CallPacked", ["pointer"], "i32")
        assert Packet(b"H", CallPacked, struct.pack("<Q", Callback["Ref"])) == struct.pack("<Bd", 0, 17)
        assert S.Dispatch("Poll")["Events"] == [{"Id": Callback["Ref"], "Args": [
            {"Tag": 2, "Count": 127, "Tail": 3}, {"Tag": 4, "Value": "123456789abcdef0", "Flags": 5}]}]
        for Handle in (Shifted, ShiftedLarge, CallPacked, Callback["Ref"]):
            assert S.Dispatch("Close", Ref=Handle)
    else:
        S.Error("MemoryLayoutRequiresPointer", "Bind", Library=S.Image, Symbol="LuiFfiSumPacked", Params=[Container["Ref"]], Result="u32")
        S.Error("MemoryLayoutRequiresPointer", "Bind", Library=S.Image, Symbol="LuiFfiAdjustPackedLarge", Params=[Large["Ref"], "u32"], Result=Large["Ref"])
        S.Error("MemoryLayoutRequiresPointer", "Callback", Params=[Packed["Ref"]], Result="void")
    S.Error("InvalidPacking", "Layout", Pack=3, Fields=[Field("Tag", "u8")])
    Memory = S.Dispatch("Allocate", Size=16384)
    Ref = Memory["Ref"]
    Bytes = bytes(range(256)) * 64
    assert Packet(b"W", Ref, struct.pack("<I", 0) + Bytes) == b"\0"
    assert Packet(b"R", Ref, struct.pack("<II", 0, len(Bytes))) == b"\0" + Bytes
    assert Packet(b"R", Ref, struct.pack("<II", len(Bytes), 0)) == b"\0"
    assert Packet(b"W", Ref, struct.pack("<I", len(Bytes)) + b"x") == b"\1OutOfBounds"
    assert Packet(b"W", Ref, struct.pack("<I", 0) + Bytes + b"x") == b"\1InvalidBytes"
    Value = {"Header": 7, "Rows": [{"Tag": 1, "Count": 250, "Tail": 3}, {"Tag": 4, "Count": 413, "Tail": 6}], "Trailer": 8}
    assert S.Dispatch("Set", Ref=Ref, Type=Container["Ref"], Value=Value)
    assert S.Dispatch("Get", Ref=Ref, Type=Container["Ref"]) == Value
    Wrapped = S.Bind("AdjustPackedInto", ["pointer", "u32"], "void")
    assert S.Dispatch("Set", Ref=Ref, Type=Packed["Ref"], Value=Value["Rows"][0])
    assert Packet(b"H", Wrapped, struct.pack("<Qd", Ref, 10)) == b"\0"
    assert S.Dispatch("Get", Ref=Ref, Type=Packed["Ref"]) == {"Tag": 1, "Count": 260, "Tail": 3}
    assert S.Dispatch("Set", Ref=Ref, Type=Container["Ref"], Value=Value)
    Sum = S.Bind("SumPacked", ["pointer"], "u32")
    assert S.Call(Sum, Memory) == 692
    for Handle in (Sum, Wrapped, Ref, Container["Ref"], Rows["Ref"], Packed["Ref"], Large["Ref"], Errors, Word, Bool, F64):
        assert S.Dispatch("Close", Ref=Handle)


def Layouts(S, Oracle, Native):
    Pair = S.Dispatch("Layout", Fields=[Field("Left", "i32"), Field("Right", "f64")])
    assert (Pair["Size"], Pair["Alignment"], Pair["Offsets"]) == (Oracle["PairBytes"], Oracle["PairAlignment"], [0, Oracle["PairRightOffset"]])
    PairType = Pair["Ref"]
    Samples = S.Dispatch("Layout", Element="u16", Count=3)["Ref"]
    Nested = S.Dispatch("Layout", Fields=[Field("Tag", "u8"), Field("Pair", PairType), Field("Samples", Samples)])
    assert (Nested["Size"], Nested["Offsets"]) == (Oracle["NestedBytes"], [0, Oracle["NestedPairOffset"], Oracle["SamplesOffset"]])
    Make = S.Bind("MakePair", ["i32", "f64"], PairType)
    Adjust = S.Bind("AdjustPair", [PairType, "i32"], PairType)
    Original = S.Call(Make, -42, .25)
    assert Original == {"Left": -42, "Right": .25} and S.Call(Adjust, Original, -20) == {"Left": -42, "Right": -19.75}
    def Packet(Opcode, Handle, *Values):
        return S.Exchange(b"LFB1" + Opcode + struct.pack("<I" + "d"*len(Values), Handle, *Values))
    assert Packet(b"A", Adjust, -42, .25, -20) == struct.pack("<Bdd", 0, -42, -19.75)
    assert Packet(b"D", Adjust, -42, .25, -20) == struct.pack("<BiIdd", 0, 0, 0, -42, -19.75)
    assert Packet(b"A", Adjust, -42, .25) == b"\1ArgumentCount"
    assert Packet(b"A", Adjust, -42, float("nan"), -20) == b"\1FloatRange"
    assert Packet(b"A", Adjust, 2147483648, .25, -20) == b"\1IntegerRange"
    assert Packet(b"A", S.Bind("EchoU64", ["u64"], "u64"), 1) == b"\1CompactSignatureRequired"
    Compatible = S.Dispatch("Layout", Pack=8, Fields=[Field("Left", "i32"), Field("Right", "f64")])
    assert [Compatible[Key] for Key in ("Size", "Alignment", "Offsets")] == [Pair[Key] for Key in ("Size", "Alignment", "Offsets")]
    PackAdjust = S.Bind("AdjustPair", [Compatible["Ref"], "i32"], Compatible["Ref"])
    assert Packet(b"A", PackAdjust, -42, .25, -20) == struct.pack("<Bdd", 0, -42, -19.75)
    Options = dict(Library=S.Image, Symbol="LuiFfiVariadicPair", Params=["i32", "i32", Compatible["Ref"], "f64"], Result="f64", Fixed=1)
    Variadic = S.Dispatch("BindVariadic", **Options)["Ref"]
    assert S.Call(Variadic, 3, 4, Original, 2) == -28.5
    assert Packet(b"A", Variadic, 3, 4, -42, .25, 2) == struct.pack("<Bd", 0, -28.5)
    assert S.Dispatch("Close", Ref=Variadic)
    Octets = S.Dispatch("Layout", Element="u8", Count=8)["Ref"]
    ByteStruct = S.Dispatch("Layout", Fields=[Field("Bytes", Octets)])
    assert (ByteStruct["Size"], ByteStruct["Alignment"]) == (8, 1)
    AdjustOctets = S.Bind("AdjustOctets", ["u8", ByteStruct["Ref"]], ByteStruct["Ref"])
    assert S.Call(AdjustOctets, 5, {"Bytes": list(range(248, 256))}) == {"Bytes": [(I + 5) % 256 for I in range(248, 256)]}
    WideArray = S.Dispatch("Layout", Element="f64", Count=40)["Ref"]
    Wide = S.Dispatch("Layout", Fields=[Field("Values", WideArray)])
    assert Wide["Size"] == 320
    AdjustWide = S.Bind("AdjustWide", [Wide["Ref"], "f64"], Wide["Ref"])
    assert S.Call(AdjustWide, {"Values": list(range(40))}, .25) == {"Values": [I + .25 for I in range(40)]}
    assert Packet(b"A", AdjustWide, *range(40), .25) == struct.pack("<B" + "d"*40, 0, *(I+.25 for I in range(40)))
    Memory = S.Dispatch("Allocate", Size=Nested["Size"])
    Data = {"Tag": 9, "Pair": Original, "Samples": [1, 2, 4]}
    assert S.Dispatch("Set", **Memory, Type=Nested["Ref"], Value=Data)
    assert S.Dispatch("Get", **Memory, Type=Nested["Ref"]) == Data
    assert S.Call(S.Bind("SumNested", ["pointer"], "f64"), Memory) == -15.75
    for Params, Result in [([Samples], "u32"), ([], Samples)]:
        S.Error("ArrayRequiresPointer", "Bind", Library=S.Image, Symbol="LuiFfiLayout", Params=Params, Result=Result)
        S.Error("ArrayRequiresPointer", "Callback", Params=Params, Result=Result)
    Mixed = S.Dispatch("Layout", Fields=[Field("Text", "pointer"), Field("Number", "f64")])["Ref"]
    # This valid static address is returned as an opaque handle, before the nonfinite field fails.
    Native.LuiFfiText.restype = C.c_void_p
    Bytes = struct.pack("@Pd", Native.LuiFfiText(), math.nan)
    assert S.Dispatch("Write", **Memory, Bytes=Bytes.hex())
    Before = S.Dispatch("Stats")
    S.Error("NonFiniteResult", "Get", **Memory, Type=Mixed)
    assert S.Dispatch("Stats") == Before
    Returned = S.Bind("PointerNumberResult", ["pointer", "u32"], Mixed)
    def ReturnedPacket(Ref, Finite):
        return S.Exchange(b"LFB1H" + struct.pack("<IQd", Returned, Ref, Finite))
    Before = S.Dispatch("Stats")
    assert ReturnedPacket(Memory["Ref"], 0) == b"\1NonFiniteResult" and S.Dispatch("Stats") == Before
    assert ReturnedPacket(2**32, 1) == b"\1InvalidBinaryArgument"
    assert ReturnedPacket(Returned, 1) == b"\1WrongHandle"
    assert ReturnedPacket(0, 1) == struct.pack("<BQd", 0, 0, 2.5)
    Status, View, Number = struct.unpack("<BQd", ReturnedPacket(Memory["Ref"], 1))
    assert Status == 0 and View != Memory["Ref"] and Number == 2.5
    assert S.Dispatch("Close", **Memory)
    assert S.Dispatch("Read", Ref=View, Size=0) == ""
    assert ReturnedPacket(Memory["Ref"], 1) == b"\1Closed"
    assert S.Dispatch("Close", Ref=View) and S.Dispatch("Close", Ref=Returned)


def Ownership(S, Oracle):
    Alloc = S.Bind("Alloc", ["u32"], "pointer")
    Free = S.Bind("Free", ["pointer"], "void")
    Live = S.Bind("Live", [], "u32")
    Hash = S.Bind("Hash", ["pointer", "u32"], "u32")
    Mutate = S.Bind("Mutate", ["pointer", "u32", "u8"], "u32")
    assert S.Call(Alloc, 0) is None and S.Call(Live) == 0
    Ptr = S.Call(Alloc, 7)
    S.Error("InvalidCount", "Adopt", **Ptr, Size=-1, Release=Free)
    assert S.Dispatch("Adopt", **Ptr, Size=7, Release=Free)
    assert S.Dispatch("Read", **Ptr, Size=7) == "00" * 7
    assert S.Dispatch("Write", **Ptr, Bytes="3700017f80ff37")
    View = S.Dispatch("Slice", **Ptr, Offset=1, Size=5)
    assert S.Call(Hash, View, 5) == int(Oracle["InputHash"], 16)
    assert S.Call(Mutate, View, 5, 90) == 5 and S.Dispatch("Read", **Ptr, Size=7) == "375a5b25daa537"
    assert S.Call(Hash, View, 5) == int(Oracle["MutatedHash"], 16)
    S.Error("OutOfBounds", "Read", **View, Size=6)
    S.Error("InvalidOwnership", "Adopt", **Ptr, Size=7, Release=Free)
    assert S.Dispatch("Close", **Ptr) and S.Call(Live) == 1
    S.Error("Closed", "Read", **Ptr, Size=1)
    assert S.Dispatch("Close", **View) and S.Call(Live) == 0
    Handle = S.Call(S.Bind("Open", ["u64"], "pointer"), "fedcba9876543210")
    assert S.Call(S.Bind("Read", ["pointer"], "u64"), Handle) == "fedcba9876543210"
    assert S.Dispatch("Adopt", **Handle, Release=S.Bind("Close", ["pointer"], "void"), Size=0)
    assert S.Dispatch("Close", **Handle) and S.Call(Live) == 0
    Before = S.Dispatch("Stats")
    Fill = [S.Dispatch("Allocate", Size=1) for _ in range(1024 - Before["Live"])]
    S.Error("HandleLimit", "Call", Function=Alloc, Args=[8])
    S.Error("HandleLimit", "Callback", Params=["i32"], Result="void")
    assert S.Call(Live) == 0
    for Item in Fill:
        assert S.Dispatch("Close", **Item)
    assert S.Dispatch("Stats") == Before


def ErrorsAndBlocking(S):
    Out = S.Dispatch("Allocate", Size=4)
    Divide = S.Bind("Divide", ["i32", "i32", "pointer"], "i32")
    LastError = S.Bind("LastError", [], "u32")
    assert S.Dispatch("Set", **Out, Type="i32", Value=27)
    assert S.Call(Divide, 1, 0, Out) == 0 and S.Call(LastError) == 2
    assert S.Dispatch("Get", **Out, Type="i32") == 27
    assert S.Call(Divide, -100, 9, Out) == 1 and S.Call(LastError) == 0
    assert S.Dispatch("Get", **Out, Type="i32") == -11 and S.Dispatch("Close", **Out)
    Wait = S.Bind("Wait", ["u32"], "u32")
    Started = time.perf_counter()
    assert S.Call(Wait, 15) == 15 and time.perf_counter() - Started >= .010
    assert S.Call(Wait, 251) == 4294967295


def Aliases(S):
    CountRelease = S.Bind("CountRelease", ["pointer"], "void")
    Count = S.Bind("ReleaseCount", [], "u32")
    Offset = S.Bind("Offset", ["pointer", "u32", "u32"], "pointer")
    Text = S.Bind("Text", [], "pointer")
    Baseline = S.Call(Count)
    # The native returns the same static address in two calls with no shared arguments.
    First, Second = S.Call(Text), S.Call(Text)
    assert S.Dispatch("Adopt", **First, Size=6, Release=CountRelease)
    S.Error("InvalidOwnership", "Adopt", **Second, Size=6, Release=CountRelease)
    S.Error("OutOfBounds", "TrustBorrow", **Second, Size=7)
    assert S.Dispatch("Close", **First) and S.Call(Count) == Baseline
    assert S.Dispatch("Close", **Second) and S.Call(Count) == Baseline + 1

    Managed = S.Dispatch("Allocate", Size=8)
    assert S.Dispatch("Write", **Managed, Bytes="0001020304050607")
    Slice = S.Dispatch("Slice", **Managed, Offset=3, Size=3)
    for Source, Size, At, Expected in [(Managed, 8, 0, "0001020304050607"), (Slice, 3, 1, "0405"), (Slice, 3, 3, "")]:
        Echo = S.Call(Offset, Source, Size, At)
        S.Error("InvalidOwnership", "Adopt", **Echo, Size=0, Release=CountRelease)
        assert S.Dispatch("Read", **Echo, Size=len(Expected)//2) == Expected
        S.Error("OutOfBounds", "Read", **Echo, Size=len(Expected)//2 + 1)
        assert S.Dispatch("Close", **Echo)
    assert S.Dispatch("Close", **Managed) and S.Dispatch("Close", **Slice)
    assert S.Call(Count) == Baseline + 1

    # Interior address escaped before the allocation size was declared.
    Raw = S.Call(S.Bind("Alloc", ["u32"], "pointer"), 8)
    Inside = S.Call(Offset, Raw, 8, 3)
    Release = S.Bind("Free", ["pointer"], "void")
    assert S.Dispatch("Adopt", **Raw, Size=8, Release=Release)
    S.Error("InvalidOwnership", "Adopt", **Inside, Size=1, Release=CountRelease)
    S.Error("OutOfBounds", "TrustBorrow", **Inside, Size=6)
    View = S.Dispatch("TrustBorrow", **Inside, Size=5)
    assert S.Dispatch("Close", **Raw) and S.Dispatch("Close", **Inside)
    assert S.Dispatch("Read", **View, Size=5) == "00" * 5
    assert S.Dispatch("Close", **View)

    # A conflicting earlier transfer uses the benign counter. The rejected
    # outer transfer succeeds after the interior ownership record retires.
    Raw = S.Call(S.Bind("Alloc", ["u32"], "pointer"), 8)
    Inside = S.Call(Offset, Raw, 8, 2)
    assert S.Dispatch("Adopt", **Inside, Size=2, Release=CountRelease)
    S.Error("InvalidOwnership", "Adopt", **Raw, Size=8, Release=Release)
    assert S.Dispatch("Close", **Inside) and S.Call(Count) == Baseline + 2
    assert S.Dispatch("Adopt", **Raw, Size=8, Release=Release) and S.Dispatch("Close", **Raw)

    RelativeText = S.Bind("Offset", ["utf8", "u32", "u32"], "pointer")
    StringView = S.Call(RelativeText, "hello", 6, 2)
    S.Error("InvalidOwnership", "Adopt", **StringView, Size=0, Release=CountRelease)
    assert S.Dispatch("Read", **StringView, Size=4) == "6c6c6f00"
    assert S.Dispatch("Close", **StringView) and S.Call(Count) == Baseline + 2


def Notifications(S):
    Callback = S.Dispatch("Callback", Params=["i32"], Result="i32", Fallback=-17)
    for Name in ["Call", "CallOnThread"]:
        Call = S.Bind(Name, ["pointer", "i32"], "i32")
        assert S.Call(Call, Callback, 23) == -17
        assert S.Dispatch("Poll") == {"Dropped": 0, "Events": [{"Id": Callback["Ref"], "Args": [23]}]}
    Notice = S.Dispatch("Callback", Params=["i32"], Result="void")
    RegisterNotice = S.Bind("Register", ["pointer"], "u32")
    Emit = S.Bind("Emit", ["i32"], "u32")
    assert S.Call(RegisterNotice, Notice) == 1
    for Index in range(67):
        assert S.Call(Emit, Index) == 1
    Queue = S.Dispatch("Poll")
    assert Queue["Dropped"] == 3 and [E["Args"][0] for E in Queue["Events"]] == list(range(64))
    assert S.Dispatch("Close", **Notice) and S.Call(Emit, 79) == 1
    assert S.Dispatch("Poll") == {"Events": [], "Dropped": 0}
    assert S.Call(S.Bind("Unregister", [], "u32")) == 1
    JobCallback = S.Dispatch("Callback", Params=["i32"], Result="void")
    Start, Cancel = S.Bind("StartJob", ["pointer", "u32"], "u32"), S.Bind("CancelJob", [], "void")
    Join = S.Bind("JoinJob", [], "u32")
    assert S.Call(Start, JobCallback, 1) == 1 and S.Call(Join) == 1
    assert S.Dispatch("Poll")["Events"][0]["Args"][0] > 0
    assert S.Call(Start, JobCallback, 4096) == 1 and S.Call(Cancel) is None and S.Call(Join) == 1
    assert S.Dispatch("Poll")["Events"] == [{"Id": JobCallback["Ref"], "Args": [-1]}]
    return Start, JobCallback


def Bench(S, OraclePath):
    Scalar = S.Bind("EchoU32", ["u32"], "u32")
    Callback = S.Dispatch("Callback", Params=["i32"], Result="i32", Fallback=7)
    Call = S.Bind("Call", ["pointer", "i32"], "i32")
    Results = {"DirectC": [], "V1JsonRoundTripNs": [], "V1CallbackWithPollNs": []}
    for _ in range(5):
        Results["DirectC"].append(json.loads(subprocess.check_output([str(OraclePath), "--benchmark"], timeout=10)))
        Started = time.perf_counter_ns()
        for Index in range(1024):
            assert S.Call(Scalar, Index) == Index
        Results["V1JsonRoundTripNs"].append((time.perf_counter_ns() - Started) / 1024)
        Started = time.perf_counter_ns()
        for Index in range(256):
            assert S.Call(Call, Callback, Index) == 7
            assert S.Dispatch("Poll")["Events"][0]["Args"] == [Index]
        Results["V1CallbackWithPollNs"].append((time.perf_counter_ns() - Started) / 256)
    Results["MedianV1JsonRoundTripNs"] = statistics.median(Results["V1JsonRoundTripNs"])
    Results["MedianV1CallbackWithPollNs"] = statistics.median(Results["V1CallbackWithPollNs"])
    return Results


def NativeWorkloads(S, Native):
    class Pair(C.Structure):
        _fields_ = [("Left", C.c_int32), ("Right", C.c_double)]

    def Prototype(Name, Parameters, Result):
        F = getattr(Native, "LuiFfi" + Name)
        F.argtypes, F.restype = Parameters, Result
        return F

    Adjust = Prototype("AdjustPair", [Pair, C.c_int32], Pair)
    Hash = Prototype("Hash", [C.c_void_p, C.c_uint32], C.c_uint32)
    Alloc = Prototype("Alloc", [C.c_uint32], C.c_void_p)
    Free = Prototype("Free", [C.c_void_p], None)
    PairType = S.Dispatch("Layout", Fields=[Field("Left", "i32"), Field("Right", "f64")])["Ref"]
    FfiAdjust = S.Bind("AdjustPair", [PairType, "i32"], PairType)
    FfiHash = S.Bind("Hash", ["pointer", "u32"], "u32")
    FfiAlloc, FfiFree = S.Bind("Alloc", ["u32"], "pointer"), S.Bind("Free", ["pointer"], "void")
    Bytes = bytes(range(256)) * 16
    Hex = Bytes.hex()
    Buffer = C.create_string_buffer(Bytes)
    FfiBuffer = S.Dispatch("Allocate", Size=len(Bytes))
    assert S.Dispatch("Write", **FfiBuffer, Bytes=Hex)
    Expected = Hash(Buffer, len(Bytes))
    Original, ForeignPair = Pair(-7, .25), {"Left": -7, "Right": .25}

    def DirectStruct():
        Result = Adjust(Original, -3)
        assert (Result.Left, Result.Right) == (-7, -2.75)
    def ProviderStruct():
        assert S.Call(FfiAdjust, ForeignPair, -3) == {"Left": -7, "Right": -2.75}
    def DirectHash():
        assert Hash(Buffer, len(Bytes)) == Expected
    def ProviderHash():
        assert S.Call(FfiHash, FfiBuffer, len(Bytes)) == Expected
    def DirectCopy():
        C.memmove(Buffer, Bytes, len(Bytes))
        assert C.string_at(Buffer, len(Bytes)) == Bytes
    def ProviderCopy():
        assert S.Dispatch("Write", **FfiBuffer, Bytes=Hex)
        assert S.Dispatch("Read", **FfiBuffer, Size=len(Bytes)) == Hex
    def DirectAllocation():
        Address = Alloc(64)
        assert Address
        Free(Address)
    def ProviderAllocation():
        Ptr = S.Call(FfiAlloc, 64)
        assert S.Dispatch("Adopt", **Ptr, Release=FfiFree, Size=64)
        assert S.Dispatch("Close", **Ptr)
    def ManagedAllocation():
        Ptr = S.Dispatch("Allocate", Size=64)
        assert S.Dispatch("Close", **Ptr)

    Operations = [DirectStruct, ProviderStruct, DirectHash, ProviderHash, DirectCopy, ProviderCopy,
                  DirectAllocation, ProviderAllocation, ManagedAllocation]
    Results = {F.__name__: [] for F in Operations}
    Count = 256
    Before = S.Dispatch("Stats")
    for F in Operations:
        F()
    for _ in range(5):
        for F in Operations:
            Started = time.perf_counter_ns()
            for _ in range(Count):
                F()
            Results[F.__name__].append((time.perf_counter_ns() - Started) / Count)
    assert S.Dispatch("Stats") == Before and Native.LuiFfiLive() == 0
    assert S.Dispatch("Close", **FfiBuffer)
    return {"BufferBytes": len(Bytes), "AllocationBytes": 64, "IterationsPerSample": Count,
            "SamplesNs": Results, "MedianNs": {Name: statistics.median(Times) for Name, Times in Results.items()}}


def ScopedCallback(S, Provider, Fixture):
    Callback = S.Dispatch("Callback", Params=["i32"], Result="i32", Fallback=-17, Computed=True)
    Remember = S.Bind("RememberComputed", ["pointer"], "void")
    Recall = S.Bind("CallRemembered", ["i32"], "i32")
    OtherThread = S.Bind("CallOnThread", ["pointer", "i32"], "i32")
    assert S.Call(Remember, Callback) is None and S.Call(Recall, 9) == -17
    Invocations = []

    @Compute
    def Double(Context, Packet, Length, Buffer, Capacity, Written):
        Request = json.loads(C.string_at(Packet, Length))
        Invocations.append((Context, Request))
        First = Request["Args"][0]
        Reply = str(51 if isinstance(First, dict) else 2 * First).encode()
        if len(Reply) > Capacity:
            return 0
        C.memmove(Buffer, Reply, len(Reply))
        Written[0] = len(Reply)
        return 1

    def Within(SessionValue, Handle, *Arguments):
        Dispatch = SessionValue.Provider.LuiFfiDispatchScopedV1
        Dispatch.argtypes = [C.c_void_p, C.c_char_p, C.c_size_t, Compute, C.c_void_p, C.POINTER(Value)]
        Packet = json.dumps({"Op": "Call", "Function": Handle, "Args": Arguments}).encode()
        Reply = Value(C.sizeof(Value))
        assert Dispatch(SessionValue.Context, Packet, len(Packet), Double, 1, C.byref(Reply))
        Result = json.loads(C.string_at(Reply.Text, Reply.TextLength))
        assert Result["Ok"], Result
        return Result["Result"]["Value"]

    assert Within(S, Recall, 9) == 18
    assert Within(S, OtherThread, Callback, 9) == -17
    Other = Session(Provider, Fixture)
    try:
        OtherRecall = Other.Bind("CallRemembered", ["i32"], "i32")
        assert Within(Other, OtherRecall, 9) == -17
    finally:
        Other.Close()
    assert Invocations == [(1, {"Id": Callback["Ref"], "Args": [9]})]
    if os.name == "nt":
        Small = S.Dispatch("Layout", Pack=2, Fields=[Field("Tag", "u8"), Field("Count", "u32"), Field("Tail", "u8")])
        Large = S.Dispatch("Layout", Pack=1, Fields=[Field("Tag", "u8"), Field("Value", "u64"), Field("Flags", "u16")])
        Packed = S.Dispatch("Callback", Params=[Small["Ref"], Large["Ref"]], Result="i32", Fallback=-7, Computed=True)
        CallPacked = S.Bind("CallPacked", ["pointer"], "i32")
        assert Within(S, CallPacked, Packed) == 51
        assert Invocations[-1] == (1, {"Id": Packed["Ref"], "Args": [
            {"Tag": 2, "Count": 127, "Tail": 3}, {"Tag": 4, "Value": "123456789abcdef0", "Flags": 5}]})
        for Ref in (Packed["Ref"], Small["Ref"], Large["Ref"], CallPacked):
            assert S.Dispatch("Close", Ref=Ref)


def Main():
    if not __debug__:
        raise RuntimeError("Run qualification with Python assertions enabled")
    if os.name == "nt":
        C.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    Parser = argparse.ArgumentParser(description=__doc__)
    for Name in ("provider", "fixture", "oracle"):
        Parser.add_argument("--" + Name, type=Path, required=True)
    Parser.add_argument("--benchmark", action="store_true")
    Options = Parser.parse_args()
    Fixture = Options.fixture.resolve()
    Native = C.CDLL(str(Fixture))
    Oracle = json.loads(subprocess.check_output([str(Options.oracle.resolve())], timeout=10))
    S = Session(Options.provider.resolve(), Fixture)
    try:
        assert S.Dispatch("Stats") == {"Live": 1, "AllocatedBytes": 0}
        assert S.Dispatch("Poll") == {"Events": [], "Dropped": 0}
        Scalars(S, Oracle)
        BinaryAndPacked(S, Native)
        Layouts(S, Oracle, Native)
        Ownership(S, Oracle)
        Aliases(S)
        ErrorsAndBlocking(S)
        Start, JobCallback = Notifications(S)
        ScopedCallback(S, Options.provider.resolve(), Fixture)
        Results = {"Passed": True, "Platform": S.Dispatch("Platform"), "Oracle": Oracle}
        if Options.benchmark:
            Results["Benchmark"] = Bench(S, Options.oracle.resolve())
            Results["Benchmark"]["NativeWorkloads"] = NativeWorkloads(S, Native)
        # Native callbacks/return paths can outlive their originating v1 session.
        assert S.Call(Start, JobCallback, 4096) == 1
    finally:
        S.Close()
    Native.LuiFfiCancelJob()
    assert Native.LuiFfiJoinJob() == 1 and Native.LuiFfiLive() == 0
    Native.LuiFfiCallRemembered.argtypes = [C.c_int32]
    Native.LuiFfiCallRemembered.restype = C.c_int32
    assert Native.LuiFfiCallRemembered(9) == -17
    Native.LuiFfiRememberComputed.argtypes = [C.c_void_p]
    Native.LuiFfiRememberComputed.restype = None
    Native.LuiFfiRememberComputed(None)
    print(json.dumps(Results))


if __name__ == "__main__":
    Main()
