--!strict
local Json = require('@Json/Json')

export type RequestOptions = {
    Method: string?,
    Value: unknown?,
    Headers: {HttpHeader}?,
    TimeoutMs: number?,
    MaxResponseBytes: number?,
}
export type Client = {
    RequestAsync: (Self: Client, Url: string, Options: RequestOptions?) -> (unknown, HttpResponse),
    GetAsync: (Self: Client, Url: string) -> (unknown, HttpResponse),
}

local JsonClient = {Version = '0.1.0', RequiredCapabilities = table.freeze({'network.client'})}

local function Fail(): never
    error('[LUI:JsonClient] InvalidOptions', 0)
end

local function Headers(Values: {HttpHeader}?): {HttpHeader}
    if Values == nil then return {} end
    if type(Values) ~= 'table' or getmetatable(Values) ~= nil then Fail() end
    local Result: {HttpHeader} = {}
    local Count = 0
    for Key, Value in Values do
        if type(Key) ~= 'number' or Key % 1 ~= 0 or Key < 1 or Key > #Values then Fail() end
        if type(Value) ~= 'table' or getmetatable(Value) ~= nil
            or type(Value.Name) ~= 'string' or type(Value.Value) ~= 'string' then Fail() end
        Count += 1
    end
    if Count ~= #Values then Fail() end
    for _, Value in Values do table.insert(Result, {Name = Value.Name, Value = Value.Value}) end
    return Result
end

function JsonClient.Open(): Client
    local Http = app:GetService('HttpService')
    local Value: any = {}
    function Value.RequestAsync(_Self: Client, Url: string, Options: RequestOptions?): (unknown, HttpResponse)
        if Options ~= nil then
            if type(Options) ~= 'table' or getmetatable(Options) ~= nil then Fail() end
            for Key in Options :: any do
                if Key ~= 'Method' and Key ~= 'Value' and Key ~= 'Headers'
                    and Key ~= 'TimeoutMs' and Key ~= 'MaxResponseBytes' then Fail() end
            end
        end
        local Settings: RequestOptions = Options or {}
        local Body = if Settings.Value ~= nil then Json.Encode(Settings.Value) else nil
        local Fields = Headers(Settings.Headers)
        if Body ~= nil then
            local HasContentType = false
            for _, Field in Fields do
                if string.lower(Field.Name) == 'content-type' then HasContentType = true end
            end
            if not HasContentType then table.insert(Fields, {Name = 'Content-Type', Value = 'application/json'}) end
        end
        local Response = Http:RequestAsync({Url = Url, Method = Settings.Method, Body = Body,
            Headers = Fields, TimeoutMs = Settings.TimeoutMs, MaxResponseBytes = Settings.MaxResponseBytes})
        return (if Response.Body == '' then nil else Json.Decode(Response.Body)), Response
    end
    function Value.GetAsync(_Self: Client, Url: string): (unknown, HttpResponse)
        return Value:RequestAsync(Url)
    end
    return table.freeze(Value) :: Client
end

return table.freeze(JsonClient)
