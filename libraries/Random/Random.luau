--!strict
local Random = {}
export type Generator = {read Number: (Self: Generator) -> number,
    read Integer: (Self: Generator, Minimum: number, Maximum: number) -> number}
local Modulus = 2147483647
function Random.New(Seed: number): Generator
    assert(type(Seed)=="number" and Seed%1==0 and Seed>=1 and Seed<Modulus,"[LUI:Random] InvalidSeed")
    local State=Seed
    local function Next(): number
        -- The largest intermediate is below 2^47, exact in a Luau number.
        State = (48271 * State) % Modulus
        return State-1
    end
    local function Number(_Self: Generator): number return Next()/(Modulus-1) end
    local function Integer(_Self: Generator, Minimum: number, Maximum: number): number
        assert(type(Minimum)=="number" and type(Maximum)=="number" and Minimum%1==0 and Maximum%1==0
            and math.abs(Minimum)<=Modulus and math.abs(Maximum)<=Modulus and Maximum>=Minimum
            and Maximum-Minimum<Modulus-1,"[LUI:Random] InvalidRange")
        local Span=Maximum-Minimum+1
        local Limit=(Modulus-1)-(Modulus-1)%Span
        local Sample=Next()
        while Sample>=Limit do Sample=Next() end
        return Minimum+Sample%Span
    end
    return table.freeze({Number=Number,Integer=Integer})
end
return table.freeze(Random)
