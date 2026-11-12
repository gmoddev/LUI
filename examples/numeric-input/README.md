# Numeric input example

An optional Luau control for a zoom setting. Use the buttons or type a finite
number and press Enter or leave the field. Values clamp to 25–400. Invalid text
restores the previous value. This development example is under qualification;
it does not establish real input-method or accessibility support.

Stage a standalone project with its explicit library files and content lock:

```text
python scripts/Stage-LibraryExample.py --example numeric-input --output build/NumericInputApp
cd build/NumericInputApp
lui check
lui run
```

No filesystem, network or other application capability is requested. The code
uses existing controls; no GTK performance implementation is imported.
