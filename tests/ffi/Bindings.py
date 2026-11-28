"""Exercise the shipped binding generator against both real Clang target ABIs."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


HeaderSource = """
typedef struct State State;
typedef const State *View;
typedef View SecondView;
typedef unsigned long NativeWord;
typedef int Scalar;
typedef Scalar Scalar;
typedef int Callback(int);
typedef int (*CallbackPointer)(int);
extern SecondView Inspect(SecondView Input, const int *const *Rows, unsigned char Bytes[8], NativeWord Size);
extern const void *Data(void);
extern const char *Lookup(State *Owner, const char *Key, char *Scratch);
extern Scalar end(Scalar);
extern int __attribute__((cdecl)) Default(int);
extern void Submit(CallbackPointer Fn);
extern void Subscribe(Callback *Fn);
extern int NoPrototype();
extern int Varargs(int Fixed, ...);
extern int TooMany(int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int);
static inline int Private(int Value) { return Value; }
extern int __attribute__((vectorcall)) Vector(int);
#if defined(_WIN32)
    extern int __attribute__((sysv_abi)) OtherAbi(int);
    extern int __attribute__((ms_abi)) PlatformAbi(int);
    extern long PlatformValue(void);
#else
    extern int __attribute__((ms_abi)) OtherAbi(int);
    extern int __attribute__((sysv_abi)) PlatformAbi(int);
    extern long long PlatformValue(void);
#endif
"""


def Check(Generator, Clang, Target, Directory):
    Header = Directory / "Bindings.h"
    Header.write_text(HeaderSource, encoding="utf-8")
    Output = Directory / "Bindings.luau"
    def Run(Names, *Tails, Text=()):
        Params = [sys.executable, str(Generator), str(Header), "--target", Target,
                  "--clang", str(Clang), "--output", str(Output)]
        for Name in Names:
            Params.extend(["--symbol", Name])
        for Tail in Tails:
            Params.extend(["--variadic-tail", Tail])
        for Option in Text:
            Params.extend(["--utf8", Option])
        return subprocess.run(Params, capture_output=True, text=True, timeout=15)
    Names = ["Inspect", "Data", "end", "Default", "PlatformValue", "PlatformAbi"]
    Generated = Run(Names)
    assert Generated.returncode == 0, Generated.stderr
    Text = Output.read_text(encoding="utf-8")
    Windows = Target == "x86_64-pc-windows-msvc"
    assert 'Ffi.Platform().OS == "%s"' % ("Windows" if Windows else "Linux") in Text
    for Name, Params, Result in [
        ("Inspect", ["pointer", "pointer", "pointer", "ulong"], "pointer"), ("Data", [], "pointer"),
        ("end", ["i32"], "i32"), ("Default", ["i32"], "i32"),
        ("PlatformValue", [], "long" if Windows else "i64"), ("PlatformAbi", ["i32"], "i32"),
    ]:
        assert '["%s"] = Keep(Image:Bind("%s", {%s}, "%s"))' % (
            Name, Name, ", ".join(json.dumps(P) for P in Params), Result) in Text, Text
    assert Run(["Data"]).returncode != 0 and Output.read_text(encoding="utf-8") == Text
    Output.unlink()
    assert 'for Index = #Opened, 1, -1 do Opened[Index]:Close() end' in Text
    assert 'if not pcall then return BindAll(), CloseAll end' in Text
    assert 'return Bindings, CloseAll' in Text
    Denied = ["NoPrototype", "Varargs", "Private", "Vector", "OtherAbi", "Submit", "Subscribe", "Missing", "TooMany"]
    for Name in Denied:
        Rejected = Run([Name])
        assert Rejected.returncode != 0 and not Output.exists(), Name
        assert "Unsupported" in Rejected.stderr or "Missing declarations" in Rejected.stderr, Rejected.stderr
    for Name, Tail in (("Varargs", "Varargs=f32"), ("Varargs", "Varargs=u16"), ("Data", "Data=i32")):
        assert Run([Name], Tail).returncode != 0 and not Output.exists()
    Generated = Run(["Varargs"], "Varargs=i32,f64,utf8")
    assert Generated.returncode == 0, Generated.stderr
    assert 'Image:BindVariadic("Varargs", 1, {"i32", "i32", "f64", "utf8"}, "i32")' in Output.read_text(encoding="utf-8")
    Output.unlink()
    for Option in ("Lookup=1", "Lookup=3", "Lookup=4", "Lookup=0", "Lookup=2,2", "Missing=result"):
        assert Run(["Lookup"], Text=[Option]).returncode != 0 and not Output.exists(), Option
    Generated = Run(["Lookup"], Text=["Lookup=2,result"])
    assert Generated.returncode == 0, Generated.stderr
    assert 'Image:Bind("Lookup", {"pointer", "utf8", "pointer"}, "utf8")' in Output.read_text(encoding="utf-8")
    return {"Target": Target, "Generated": [*Names, "Varargs", "Lookup"], "Rejected": Denied}


def Main():
    if not __debug__:
        raise RuntimeError("Run qualification with Python assertions enabled")
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--clang", default="clang")
    Parser.add_argument("--generator", type=Path, default=Path(__file__).resolve().parents[2] / "scripts/Generate-FfiBindings.py")
    Options = Parser.parse_args()
    Version = subprocess.check_output([Options.clang, "--version"], text=True, timeout=5).splitlines()[0]
    Results = []
    for Target in ("x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu"):
        with tempfile.TemporaryDirectory() as Directory:
            Results.append(Check(Options.generator, Options.clang, Target, Path(Directory)))
    print(json.dumps({"Passed": True, "Compiler": Version, "Targets": Results}))


if __name__ == "__main__":
    Main()
