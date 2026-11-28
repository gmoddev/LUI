--!strict
local Utc = {Version = "0.1.0", RequiredCapabilities = table.freeze({})}
local MonthLength = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}

local function BeforeYear(Year: number): number
    local Previous = Year - 1
    return Previous * 365 + math.floor(Previous / 4) - math.floor(Previous / 100) + math.floor(Previous / 400)
end

local Epoch = BeforeYear(1970)
local Minimum = -Epoch * 86400
local Maximum = (BeforeYear(10000) - Epoch) * 86400 - 1

local function Days(Year: number, Month: number): number
    if Month == 2 and Year % 4 == 0 and (Year % 100 ~= 0 or Year % 400 == 0) then return 29 end
    return MonthLength[Month]
end

local function Check(Seconds: number)
    if type(Seconds) ~= "number" or Seconds ~= math.floor(Seconds) or Seconds < Minimum or Seconds > Maximum then
        error("[LUI:Utc] TimestampOutOfRange", 0)
    end
end

function Utc.Parse(Text: string): number
    if type(Text) ~= "string" or (#Text ~= 20 and #Text ~= 25) then error("[LUI:Utc] InvalidTimestamp", 0) end
    local Y, M, D, H, N, S, Zone = string.match(Text, "^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)(.+)$")
    if not Zone then error("[LUI:Utc] InvalidTimestamp", 0) end
    local Year, Month, Day = tonumber(Y) :: number, tonumber(M) :: number, tonumber(D) :: number
    local Hour, Minute, Second = tonumber(H) :: number, tonumber(N) :: number, tonumber(S) :: number
    if Year < 1 or Month < 1 or Month > 12 or Day < 1 or Day > Days(Year, Month)
        or Hour > 23 or Minute > 59 or Second > 59 then error("[LUI:Utc] InvalidTimestamp", 0) end
    local Offset = 0
    if Zone ~= "Z" then
        local Sign, OH, OM = string.match(Zone, "^([+-])(%d%d):(%d%d)$")
        if not Sign or Zone == "-00:00" then error("[LUI:Utc] InvalidOffset", 0) end
        local Hours, Minutes = tonumber(OH) :: number, tonumber(OM) :: number
        if Hours > 23 or Minutes > 59 then error("[LUI:Utc] InvalidOffset", 0) end
        Offset = (Hours * 60 + Minutes) * (if Sign == "-" then -60 else 60)
    end
    local Ordinal = BeforeYear(Year) + Day - 1
    for Previous = 1, Month - 1 do Ordinal += Days(Year, Previous) end
    local Seconds = (Ordinal - Epoch) * 86400 + Hour * 3600 + Minute * 60 + Second - Offset
    Check(Seconds)
    return Seconds
end

function Utc.Format(Seconds: number): string
    Check(Seconds)
    local Ordinal = math.floor(Seconds / 86400) + Epoch
    local Low, High = 1, 10000
    while High - Low > 1 do
        local Middle = math.floor((High + Low) / 2)
        if BeforeYear(Middle) <= Ordinal then Low = Middle else High = Middle end
    end
    local Day = Ordinal - BeforeYear(Low) + 1
    local Month = 1
    while Day > Days(Low, Month) do
        Day -= Days(Low, Month)
        Month += 1
    end
    return string.format("%04d-%02d-%02dT%02d:%02d:%02dZ", Low, Month, Day,
        math.floor(Seconds / 3600) % 24, math.floor(Seconds / 60) % 60, Seconds % 60)
end

return table.freeze(Utc)
