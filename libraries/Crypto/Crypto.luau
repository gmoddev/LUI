--!strict
local Crypto = {}
function Crypto.Sha256(Bytes: string): string
    return app:GetService("Crypto"):Sha256(Bytes)
end
function Crypto.HmacSha256(Key: string, Bytes: string): string
    return app:GetService("Crypto"):HmacSha256(Key, Bytes)
end
function Crypto.RandomBytes(Count: number): string
    return app:GetService("Crypto"):RandomBytes(Count)
end
function Crypto.HkdfSha256(Secret: string, Salt: string, Info: string, Count: number): string
    return app:GetService("Crypto"):HkdfSha256(Secret, Salt, Info, Count)
end
function Crypto.Seal(Key: string, Plaintext: string, AssociatedData: string?): string
    return app:GetService("Crypto"):Seal(Key, Plaintext, if AssociatedData == nil then "" else AssociatedData)
end
function Crypto.Open(Key: string, Envelope: string, AssociatedData: string?): string
    return app:GetService("Crypto"):Open(Key, Envelope, if AssociatedData == nil then "" else AssociatedData)
end
return table.freeze(Crypto)
