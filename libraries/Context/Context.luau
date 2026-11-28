--!strict
local Context = {}
function Context.Get(Key: string): string?
    local Host = app:GetService("Context")
    return if Host:Has(Key) then Host:Read(Key) else nil
end
function Context.Platform(): string return app:GetService("Context"):Platform() end
function Context.Elapsed(): number return app:GetService("Context"):Elapsed() end
return table.freeze(Context)
