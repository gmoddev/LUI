--!strict
local Process = require("@Process/Process")
local Json = require("@Json/Json")
local Language = {}
export type Diagnostic = {Message: string, StartLine: number, StartColumn: number, EndLine: number, EndColumn: number}
export type Import = {Path: string, StartLine: number, StartColumn: number, EndLine: number, EndColumn: number}
export type Report = {Valid: boolean, BytecodeBytes: number, Diagnostics: {Diagnostic}, Imports: {Import}, Truncated: boolean}
function Language.Analyze(Source: string, Complete: (Report?, string?) -> ()): Process.Request
    assert(#Source<=4096 and type(Complete)=="function","[LUI:Language] InvalidArgument")
    return Process.Run({Arguments={"--inspect-source"},Input=Source,MaxOutputBytes=32768},function(Result)
        if Result.Status~="Exited" or Result.ExitCode~=0 then Complete(nil,if Result.Status=="Exited" then "ToolFailed" else Result.Status); return end
        local Ok,Report=pcall(function(): Report
            local Value=Json.Decode(Result.Stdout,{MaxNodes=1024,MaxDepth=4,MaxBytes=32768})
            assert(type(Value)=="table" and type(Value.Valid)=="boolean" and type(Value.BytecodeBytes)=="number" and type(Value.Truncated)=="boolean")
            assert(type(Value.Diagnostics)=="table" and type(Value.Imports)=="table" and getmetatable(Value.Diagnostics)==getmetatable(Json.Array({})) and getmetatable(Value.Imports)==getmetatable(Json.Array({})) and #Value.Diagnostics<=16 and #Value.Imports<=32)
            for _,List in {Value.Diagnostics,Value.Imports} do
                for _,Item in List do
                    assert(type(Item)=="table" and type(if List==Value.Diagnostics then Item.Message else Item.Path)=="string")
                    for _,Key in {"StartLine","StartColumn","EndLine","EndColumn"} do
                        assert(type(Item[Key])=="number" and Item[Key]>=1 and Item[Key]%1==0)
                    end
                end
            end
            return Value :: Report
        end)
        if Ok then Complete(Report,nil) else Complete(nil,"InvalidReport") end
    end)
end
return table.freeze(Language)
