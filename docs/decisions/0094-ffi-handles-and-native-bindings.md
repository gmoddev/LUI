# 0094. FFI handles and native bindings

Prepared signatures with opaque handles or exact words can use fixed-size
packets. A handle field carries a session reference or zero for a null
pointer. The provider checks the reference type, retains each argument
through the call and creates returned handles through the same ownership
registry as structured calls. It preflights handle capacity and rolls back
new handles if result conversion fails. Exact words keep all 64 bits and
retain the sixteen-digit hexadecimal Luau representation. Larger layouts,
UTF-8 strings and custom aggregate table behavior use the structured path.
The owner and trusted-worker hosts negotiate the compact path and retain
their existing independent sessions and admission rules.

Microsoft x64 classifies fixed packed POD callback arguments by size.
The Windows C oracle and provider exercise both an eight-byte register
argument and an eleven-byte indirect argument in the same closure. Both
supported x64 ABIs admit natural by-value aggregates in a variadic tail.
The C oracle uses a mixed tail with an integer, a pair and a double.
Layout-changing packed variadic tails, pointer-bearing callback parameters,
aggregate callback returns, unions and bitfields remain outside the profile.

System V AMD64 classifies aggregates with unaligned fields for memory
transfer. The pinned libffi classifier derives field placement from the
natural alignment of its element types. Its layout description lacks
arbitrary field offsets. Use a fixed C wrapper with pointer parameters
when a layout changes the compiler's ABI classification. The wrapper
uses the original C declaration for the by-value call, then writes the
result to caller-owned storage. This pattern is exercised by the C oracle
and provider on both platforms.

```c
void AdjustPackedInto(Packed *Value, uint32_t Delta) {
    *Value = AdjustPacked(*Value, Delta);
}
```

Generated binding factories take an open image and return named functions
and an idempotent cleanup function. The caller retains responsibility for
closing the image and releasing objects owned by the native library.
Explicit `--utf8` positions accept only `const char *` declarations.
Input storage lasts through the call. Returned strings are copied before
the call returns to Luau. Use pointer arguments when the API retains
its input or uses a different encoding or byte-span contract.

Use trusted persistent actors for owner-independent headless calls. A
blocked call holds its worker session and slot until it returns. Delivery
can be canceled independently. Desktop libraries with asynchronous jobs
can expose start, cancel and join, with notifications delivered later
through the owner. Use the broker for a child-process timeout boundary.
An OS execution policy provides any additional confinement.
