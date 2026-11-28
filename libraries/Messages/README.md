# Messages

`Messages.Encode(Value)` returns an immutable versioned string. `Messages.Decode(Packet)` returns a fresh buffer or a decoded JSON value. The five-byte prefix identifies version one and its payload type. Packets have a 64 KiB ceiling, including the header. JSON uses the existing 16-level, 2048-node profile and rejects cycles, functions, Instances and native handles. Top-level buffers preserve arbitrary bytes. No capabilities are required.

~~~luau
local Messages = require("@Messages/Messages")
local Queue = Messages.Mailbox(4, 65536)
assert(Queue:Send({Job = "render"}))
local Received, Failure = Queue:Receive()
assert(not Failure and Received.Job == "render")
Queue:Close()
~~~

A mailbox stores at most 1–64 packets and 5–262144 aggregate bytes. Defaults are 32 packets and 262144 bytes. Send returns false and Full or Closed on backpressure. Receive returns nil and Empty or Closed if no message is available. Usage returns packet and byte counts. Close discards queued data and retires the mailbox. Invalid values or packets raise an error. The mailbox is owned by one Luau VM. Its packet strings can cross a separately implemented transport by value. This module does not create or schedule other VMs.
