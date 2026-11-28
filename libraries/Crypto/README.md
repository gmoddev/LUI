# Crypto

Use the optional `Crypto` provider with `NativeExtensions` and `HostServices` in the manifest and `--providers Crypto` on the headless command line. Copy the target SDK's `Crypto.json` declaration and its `Native` image into the project. The standard host checks the captured image against its compiled digest.

~~~luau
local Crypto = require("@Crypto/Crypto")
local Digest = Crypto.Sha256("abc")
assert(#Digest == 32)
~~~

`Sha256(Bytes)` accepts up to 65,536 bytes. `HmacSha256(Key, Bytes)` accepts a 1–4,096 byte key and up to 65,536 bytes of input. Both return a 32-byte binary string. `RandomBytes(Count)` accepts an integer from 1 to 4,096. It returns exactly that many cryptographically random bytes. These bounded calls execute synchronously. Invalid inputs or provider errors raise a catchable error. Returned strings belong to Luau and follow its ordinary garbage collection. The provider uses Windows CNG or Linux OpenSSL 3.0+.

The module supplies primitives for hashing and message authentication. Key storage and secret lifetime in application memory belong to the application.

`HkdfSha256(Secret, Salt, Info, Count)` performs HKDF extract-and-expand. Supply 16–2,048 bytes of high-entropy key material, up to 1,024 bytes each of salt and context, and a derived length of 1–64 bytes. Empty salt and context are supported. Password hashing requires a separate password KDF.

`Seal(Key, Plaintext, AssociatedData?)` uses AES-256-GCM and a newly generated 96-bit nonce on each call. Supply a 32-byte key, up to 65,536 bytes of plaintext and up to 4,096 bytes of associated data. The binary envelope contains a version byte of 1, twelve nonce bytes, ciphertext and a 16-byte authentication tag. `Open(Key, Envelope, AssociatedData?)` returns the plaintext only after authentication succeeds. A wrong key, tag, version or associated data raises a catchable error. Both APIs operate on bounded data synchronously. The application owns key generation and storage. Rotate each key before it reaches 2^32 encryptions across all applications and devices that use it.

~~~luau
local Key = Crypto.RandomBytes(32)
local Sealed = Crypto.Seal(Key, "record bytes", "format-v1")
local Original = Crypto.Open(Key, Sealed, "format-v1")
~~~

[HKDF test vectors](https://datatracker.ietf.org/doc/rfc5869/) · [Windows key derivation](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptkeyderivation) · [OpenSSL HKDF](https://docs.openssl.org/3.0/man7/EVP_KDF-HKDF/)
