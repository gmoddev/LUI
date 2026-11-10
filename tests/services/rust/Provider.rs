#![allow(non_snake_case)]
use std::collections::BTreeMap;
use std::ffi::{c_char, c_void};
use std::mem::{offset_of, size_of};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::{self, JoinHandle};

// Deliberately independent declarations: no generated C bindings or C++ code.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Id { Session: u64, Value: u64 }
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Method {
    StructSize: u32, AbiVersion: u32, Name: *const c_char,
    MaxInputBytes: u32, MaxResultBytes: u32, InputKind: u32, ResultKind: u32,
    DeadlineMilliseconds: u32, Reserved: u32,
}
unsafe impl Sync for Method {}
#[repr(C)]
pub struct Service {
    StructSize: u32, AbiVersion: u32, Name: *const c_char, Methods: *const Method,
    MethodCount: u32, ResourceKindCount: u32, RequiredCapabilities: u64,
    Version: u32, Reserved: u32,
}
#[repr(C)]
pub struct Provider {
    StructSize: u32, AbiVersion: u32,
    Start: extern "C" fn(*mut c_void, Id, u32, Id, Id),
    Cancel: extern "C" fn(*mut c_void, Id),
    CloseHandle: extern "C" fn(*mut c_void, Id, u32),
    BeginShutdown: extern "C" fn(*mut c_void),
    PollShutdown: extern "C" fn(*mut c_void) -> u32,
    Destroy: extern "C" fn(*mut c_void),
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Host {
    StructSize: u32, AbiVersion: u32, Session: *mut c_void,
    RetainSession: extern "C" fn(*mut c_void) -> u32,
    ReleaseSession: extern "C" fn(*mut c_void) -> u32,
    CopyBytes: extern "C" fn(*mut c_void, Id, *const c_void, u64, *mut Id) -> u32,
    RetainBytes: extern "C" fn(*mut c_void, Id) -> u32,
    ReleaseBytes: extern "C" fn(*mut c_void, Id) -> u32,
    ViewBytes: extern "C" fn(*mut c_void, Id, *mut *const c_void, *mut u64) -> u32,
    CreateHandle: extern "C" fn(*mut c_void, u32, *mut Id) -> u32,
    RetainHandle: extern "C" fn(*mut c_void, Id) -> u32,
    ReleaseHandle: extern "C" fn(*mut c_void, Id) -> u32,
    Complete: extern "C" fn(*mut c_void, Id, u32, Id, Id) -> u32,
    Emit: extern "C" fn(*mut c_void, Id, Id) -> u32,
}
// The C host guarantees thread-safe entry points and explicit leases.
unsafe impl Send for Host {}
unsafe impl Sync for Host {}
const _: () = {
    assert!(size_of::<usize>() == 8);
    assert!(size_of::<Id>() == 16);
    assert!(size_of::<Method>() == 40 && offset_of!(Method, MaxInputBytes) == 16);
    assert!(size_of::<Service>() == 48 && offset_of!(Service, RequiredCapabilities) == 32);
    assert!(size_of::<Provider>() == 56);
    assert!(size_of::<Host>() == 104 && offset_of!(Host, Complete) == 88);
};

const NONE: Id = Id { Session: 0, Value: 0 };
const fn Descriptor(Name: &'static [u8], Input: u32, Output: u32, InputKind: u32, ResultKind: u32) -> Method {
    Method { StructSize: size_of::<Method>() as u32, AbiVersion: 2, Name: Name.as_ptr().cast(),
        MaxInputBytes: Input, MaxResultBytes: Output, InputKind, ResultKind,
        DeadlineMilliseconds: 30000, Reserved: 0 }
}
static METHODS: [Method; 9] = [
    Descriptor(b"Echo\0", 8*1024*1024, 8*1024*1024, 0, 0),
    Descriptor(b"Counter\0", 0, 8, 0, 1),
    Descriptor(b"Increment\0", 0, 8, 1, 0),
    Descriptor(b"Hold\0", 65536, 65536, 0, 0),
    Descriptor(b"Wake\0", 0, 0, 0, 0),
    Descriptor(b"Fail\0", 0, 0, 0, 0),
    Descriptor(b"Progress\0", 65536, 65536, 0, 0),
    Descriptor(b"StallShutdown\0", 0, 0, 0, 0),
    Descriptor(b"Frame\0", 0, 16, 0, 0),
];

struct State {
    Threads: Vec<JoinHandle<()>>, Counters: BTreeMap<u64, u64>,
    Canceled: BTreeMap<u64, bool>, Closed: bool, Open: bool, Stuck: bool,
}
struct Context { Host: Host, State: Mutex<State>, Changed: Condvar }
struct Work { Owner: Arc<Context>, Input: Id, Handle: Id }
impl Drop for Work {
    fn drop(&mut self) {
        let Host = self.Owner.Host;
        if self.Handle.Value != 0 { (Host.ReleaseHandle)(Host.Session, self.Handle); }
        (Host.ReleaseBytes)(Host.Session, self.Input);
        (Host.ReleaseSession)(Host.Session);
    }
}
fn Guard(Action: impl FnOnce() -> u32) -> u32 {
    catch_unwind(AssertUnwindSafe(Action)).unwrap_or(9)
}
unsafe fn CloneContext(Pointer: *mut c_void) -> Arc<Context> {
    let Pointer = Pointer.cast::<Context>();
    Arc::increment_strong_count(Pointer); Arc::from_raw(Pointer)
}
fn Result(Context: &Context, Request: Id, Bytes: &[u8], Handle: Id) {
    let Host = Context.Host; let mut Buffer = NONE;
    let Status = (Host.CopyBytes)(Host.Session, Request, Bytes.as_ptr().cast(), Bytes.len() as u64, &mut Buffer);
    if Status == 0 {
        (Host.Complete)(Host.Session, Request, 0, Buffer, Handle);
        (Host.ReleaseBytes)(Host.Session, Buffer);
    } else { (Host.Complete)(Host.Session, Request, 9, NONE, NONE); }
}
extern "C" fn Start(Pointer: *mut c_void, Request: Id, Method: u32, Input: Id, Handle: Id) {
    let Status = Guard(|| {
        let Context = unsafe { CloneContext(Pointer) }; let Host = Context.Host;
        {
            let mut State = Context.State.lock().unwrap();
            let mut Index = 0;
            while Index < State.Threads.len() {
                if State.Threads[Index].is_finished() { let _ = State.Threads.swap_remove(Index).join(); }
                else { Index += 1; }
            }
            if State.Closed { return 8; }
            if Method == 7 {
                State.Stuck = true; drop(State); (Host.Complete)(Host.Session, Request, 0, NONE, NONE); return 0;
            }
            if Method == 4 {
                State.Open = true; Context.Changed.notify_all();
                drop(State); (Host.Complete)(Host.Session, Request, 0, NONE, NONE); return 0;
            }
            if Method == 5 { drop(State); (Host.Complete)(Host.Session, Request, 9, NONE, NONE); return 0; }
            // A completed but unjoined worker still consumes a slot. Do not
            // accumulate thread records if the application continuously calls.
            if State.Threads.len() >= 32 { return 4; }
            State.Canceled.insert(Request.Value, false);
        }
        if (Host.RetainSession)(Host.Session) != 0 { return 8; }
        if (Host.RetainBytes)(Host.Session, Input) != 0 { (Host.ReleaseSession)(Host.Session); return 1; }
        if Handle.Value != 0 && (Host.RetainHandle)(Host.Session, Handle) != 0 {
            (Host.ReleaseBytes)(Host.Session, Input); (Host.ReleaseSession)(Host.Session); return 8;
        }
        let Work = Work { Owner: Context.clone(), Input, Handle };
        let Worker = thread::Builder::new().name("LuiNativeProof".into()).spawn(move || {
            let _ = Guard(|| {
                let Context = &Work.Owner; let Host = Context.Host;
                if Method == 3 {
                    let mut State = Context.State.lock().unwrap();
                    while !State.Closed && !State.Open && !State.Canceled.get(&Request.Value).copied().unwrap_or(true) {
                        State = Context.Changed.wait(State).unwrap();
                    }
                }
                let Canceled = {
                    let State = Context.State.lock().unwrap();
                    State.Closed || State.Canceled.get(&Request.Value).copied().unwrap_or(true)
                };
                if Canceled { (Host.Complete)(Host.Session, Request, 6, NONE, NONE); }
                else if Method == 1 {
                    let mut Counter = NONE;
                    if (Host.CreateHandle)(Host.Session, 1, &mut Counter) == 0 {
                        Context.State.lock().unwrap().Counters.insert(Counter.Value, 0);
                        Result(Context, Request, &0u64.to_le_bytes(), Counter);
                        (Host.ReleaseHandle)(Host.Session, Counter);
                    } else { (Host.Complete)(Host.Session, Request, 4, NONE, NONE); }
                } else if Method == 2 {
                    let Number = {
                        let mut State = Context.State.lock().unwrap();
                        State.Counters.get_mut(&Handle.Value).map(|Value| { *Value += 1; *Value })
                    };
                    if let Some(Number) = Number { Result(Context, Request, &Number.to_le_bytes(), NONE); }
                    else { (Host.Complete)(Host.Session, Request, 8, NONE, NONE); }
                } else if Method == 8 {
                    // 2x2 premultiplied BGRA, synthetic terminal owned bytes.
                    Result(Context, Request, &[11,22,33,255, 44,55,66,255, 77,88,99,255, 111,122,133,255], NONE);
                } else {
                    let mut Data = std::ptr::null(); let mut Length = 0;
                    if (Host.ViewBytes)(Host.Session, Input, &mut Data, &mut Length) == 0 {
                        let Bytes = if Length == 0 { &[] } else { unsafe { std::slice::from_raw_parts(Data.cast::<u8>(), Length as usize) } };
                        if Method == 6 { (Host.Emit)(Host.Session, Request, Input); }
                        Result(Context, Request, Bytes, NONE);
                    } else { (Host.Complete)(Host.Session, Request, 9, NONE, NONE); }
                }
                Context.State.lock().unwrap().Canceled.remove(&Request.Value); 0
            });
            drop(Work);
        });
        match Worker {
            Ok(Worker) => { Context.State.lock().unwrap().Threads.push(Worker); 0 }
            Err(_) => { Context.State.lock().unwrap().Canceled.remove(&Request.Value); 9 }
        }
    });
    if Status != 0 && !Pointer.is_null() {
        let Context = unsafe { &*Pointer.cast::<Context>() };
        (Context.Host.Complete)(Context.Host.Session, Request, Status, NONE, NONE);
    }
}
extern "C" fn Cancel(Pointer: *mut c_void, Request: Id) {
    let _ = Guard(|| {
        let Context = unsafe { &*Pointer.cast::<Context>() };
        if let Some(Value) = Context.State.lock().unwrap().Canceled.get_mut(&Request.Value) { *Value = true; }
        Context.Changed.notify_all(); 0
    });
}
extern "C" fn CloseHandle(Pointer: *mut c_void, Handle: Id, _Kind: u32) {
    let _ = Guard(|| { unsafe { &*Pointer.cast::<Context>() }.State.lock().unwrap().Counters.remove(&Handle.Value); 0 });
}
extern "C" fn BeginShutdown(Pointer: *mut c_void) {
    if Pointer.is_null() { return; }
    let _ = Guard(|| {
        let Context = unsafe { &*Pointer.cast::<Context>() };
        Context.State.lock().unwrap().Closed = true; Context.Changed.notify_all(); 0
    });
}
extern "C" fn PollShutdown(Pointer: *mut c_void) -> u32 {
    if Pointer.is_null() { return 1; }
    u32::from(Guard(|| {
        let Context = unsafe { &*Pointer.cast::<Context>() };
        let Threads = {
            let mut State = Context.State.lock().unwrap();
            if State.Stuck { return 0; }
            if State.Threads.iter().any(|Thread| !Thread.is_finished()) { return 0; }
            std::mem::take(&mut State.Threads)
        };
        for Thread in Threads { if Thread.join().is_err() { return 0; } }
        1
    }) == 1)
}
extern "C" fn Destroy(Pointer: *mut c_void) {
    if !Pointer.is_null() { let _ = Guard(|| { drop(unsafe { Arc::from_raw(Pointer.cast::<Context>()) }); 0 }); }
}
#[no_mangle]
pub unsafe extern "C" fn LuiNativeQuery(Output: *mut Service, Callbacks: *mut Provider) -> u32 {
    Guard(|| {
        if Output.is_null() || Callbacks.is_null() || (*Output).StructSize != size_of::<Service>() as u32 ||
            (*Output).AbiVersion != 2 || (*Callbacks).StructSize != size_of::<Provider>() as u32 || (*Callbacks).AbiVersion != 2 { return 1; }
        *Output = Service { StructSize: size_of::<Service>() as u32, AbiVersion: 2, Name: b"RustProof\0".as_ptr().cast(),
            Methods: METHODS.as_ptr(), MethodCount: METHODS.len() as u32, ResourceKindCount: 1, RequiredCapabilities: 3, Version: 1, Reserved: 0 };
        *Callbacks = Provider { StructSize: size_of::<Provider>() as u32, AbiVersion: 2,
            Start, Cancel, CloseHandle, BeginShutdown, PollShutdown, Destroy }; 0
    })
}
#[no_mangle]
pub unsafe extern "C" fn LuiNativeInit(Input: *const Host, Output: *mut *mut c_void) -> u32 {
    Guard(|| {
        if Input.is_null() || Output.is_null() || (*Input).StructSize != size_of::<Host>() as u32 || (*Input).AbiVersion != 2 { return 1; }
        *Output = Arc::into_raw(Arc::new(Context { Host: *Input, Changed: Condvar::new(), State: Mutex::new(State {
            Threads: Vec::new(), Counters: BTreeMap::new(), Canceled: BTreeMap::new(), Closed: false, Open: false, Stuck: false,
        }) })).cast_mut().cast(); 0
    })
}
