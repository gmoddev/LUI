import { readFile as ReadFile, writeFile as WriteFile, mkdir as MakeDirectory } from 'node:fs/promises';
import Path from 'node:path';
import { ModuleDeclarations } from './LibraryReference.mjs';

const Start = `local Window = Instance.new("Window", {Title = "Library example", Size = UDim2.fromOffset(640, 300)})
local Status = Instance.new("TextLabel", {Parent = Window, Text = "Ready",
    Position = UDim2.fromOffset(16, 190), Size = UDim2.fromOffset(600, 72)})
`;
const Finish = '\nWindow.Visible = true\n';
const Example = (Code, Outcome, Starter = 'workbench') => ({Code:Start+Code+Finish,Outcome,Starter});
export const LibraryExamples = {
  Commands: Example(`local Commands = require("@LuiWorkbench/Commands")
local Registry = Commands.New()
Registry:Register({Id = "Greeting", Label = "Say hello", Execute = function() Status.Text = "Hello" end})
local Button = Instance.new("TextButton", {Parent = Window, Text = "Say hello", Size = UDim2.fromOffset(220, 44)})
Button.Activated:Connect(function() Registry:Execute("Greeting") end)
Window.Destroying:Connect(function() Registry:Dispose() end)`, 'The button invokes a named action. You can invoke the same action from a palette or shortcut without duplicating the application callback.'),
  Documents: Example(`local Documents = require("@LuiWorkbench/Documents")
local Store = Documents.New()
Store:Open("Notes", "Notes.txt", "Original")
Store:SetText("Notes", "Edited")
local Closed, Reason = Store:Close("Notes")
Status.Text = if Closed then "Closed" else "Close refused: " .. (Reason or "Unknown")
Window.Destroying:Connect(function() Store:Dispose() end)`, 'The status says Close refused: Dirty. Documents tracks editing state; this example does not claim to save a file.'),
  Workspace: Example(`local Workspace = require("@LuiWorkbench/Workspace")
local Layout = Workspace.New()
Layout:Add("Notes", "Left")
Layout:Add("Preview", "Right")
local Left, Right, IsSplit = Layout:Layout(600)
Status.Text = "Two panes: " .. tostring(IsSplit) .. "; widths: " .. Left .. ", " .. Right
Window.Destroying:Connect(function() Layout:Dispose() end)`, 'Displays the widths calculated by the model. Workspace stores tab/layout state; it does not create windows, editors or visual panes.'),
  Split: Example(`local Split = require("@LuiWorkbench/Split")
local Drag = Split.New()
local Ratio: number = 0.5
local Width = 600
local Grip = Instance.new("Frame", {Parent = Window, BackgroundColor3 = Color3.fromRGB(100, 120, 180),
    Position = UDim2.fromOffset(Ratio * (Width - 8), 0), Size = UDim2.fromOffset(8, 160)})
local function Apply(Next: number?)
    if Next then Ratio = Next; Grip.Position = UDim2.fromOffset(Ratio * (Width - 8), 0) end
    Status.Text = "Divider ratio: " .. Ratio
end
Grip.InputBegan:Connect(function(Input)
    if Input.Device == "Mouse" then Drag:Begin(Input, Grip.AbsolutePosition.X + Input.Position.X, Width, Ratio) end
end)
Grip.InputChanged:Connect(function(Input)
    if Input.Device == "Mouse" then Apply(Drag:Move(Input, Grip.AbsolutePosition.X + Input.Position.X, Width)) end
end)
Grip.InputEnded:Connect(function(Input) Apply(Drag:Finish(Input)) end)
Window.Destroying:Connect(function() Drag:Dispose() end)`, 'Drag the thin divider with the primary mouse button. The example uses a fixed 600-unit viewport; a responsive view must cancel an active drag when that viewport changes.'),
  Json: Example(`local Json = require("@Json/Json")
local Encoded = Json.Encode({Name = "Notes", Archived = false, Tags = Json.Array(), Settings = Json.Object(), DeletedAt = Json.Null})
local Decoded = Json.Decode(Encoded)
Status.Text = Encoded
local Ok = pcall(function() Json.Decode("{broken}") end)
assert(not Ok) -- Handle malformed input without exposing its contents.`, 'Displays JSON with an explicit empty array, empty object and null value. Decoding malformed JSON fails through pcall.', 'json-client'),
  JsonClient: Example(`local JsonClient = require("@JsonClient/JsonClient")
local Client = JsonClient.Open()
task.spawn(function()
    local Ok, Data, Response = pcall(function()
        return Client:GetAsync("http://127.0.0.1:8080/status")
    end)
    if not Ok then Status.Text = "Request or JSON decoding failed"; return end
    Status.Text = "HTTP " .. Response.StatusCode
end)`, 'With a local JSON endpoint on port 8080, displays its HTTP status. Without one, reports failure. Data is decoded JSON; validate its shape before using fields.', 'json-client'),
  NumericInput: Example(`local NumericInput = require("@LuiUI/NumericInput")
local Zoom = NumericInput.New(Window, {Value = 100, Minimum = 25, Maximum = 400, Step = 25,
    Label = "Zoom percent", OnChanged = function(Value) Status.Text = "Zoom: " .. Value .. "%" end})
Zoom.View.Position = UDim2.fromOffset(16, 16)
Zoom.View.Size = UDim2.fromOffset(300, 36)
Window.Destroying:Connect(function() Zoom:Dispose() end)`, 'A number field and two step buttons update the zoom value. This is a source component built from ordinary LUI controls, not a new core Instance class.', 'numeric-input'),
};
// One operation at a time; enable saving only after the read's callback.
LibraryExamples.Preferences = Example(`local Preferences = require("@LuiWorkbench/Preferences")
local Store = Preferences.New(app:GetService("StorageService"), "example_theme")
local Button = Instance.new("TextButton", {Parent = Window, Text = "Save theme", Enabled = false, Size = UDim2.fromOffset(220, 44)})
Store:Read(function(Bytes, Failure)
    Status.Text = if Failure then "Read failed" elseif Bytes then buffer.tostring(Bytes) else "No theme saved yet"
    Button.Enabled = true
end)
Button.Activated:Connect(function()
    Button.Enabled = false
    Store:Write(buffer.fromstring("Dark"), function(_, Failure)
        Status.Text = if Failure then "Save failed" else "Theme saved"
        Button.Enabled = true
    end)
end)
Window.Destroying:Connect(function() Store:Dispose() end)`, 'Reads the saved theme, then enables a Save theme button. Save, close and reopen the same project to read Dark. A copied project has separate storage.');

const Intros = {
  Commands: 'Keep actions in one registry so a button, palette and keyboard shortcut can invoke the same code. The registry does not draw a menu or palette for you.',
  Documents: 'Track named text buffers, edits and pending loads/saves. This is a document model, not a file API: your app performs the actual authorized read or write.',
  Workspace: 'Track which known document IDs belong in two panes and calculate their widths. This is a layout model, not a docking-window framework.',
  Split: 'Track one primary-mouse divider drag. Your view supplies input positions and applies the returned ratio; the module does not create or own that view.',
  Preferences: 'Read and write small, ordinary app settings through scoped StorageService. Data is unencrypted; do not use it for passwords or tokens.',
  Json: 'Convert between Luau values and JSON text. No network or storage permission is needed to use the codec.',
  JsonClient: 'Call an HTTP endpoint and decode a JSON response. It uses HttpService and the Json codec; it does not validate your application’s response schema.',
  NumericInput: 'Offer a bounded number field with decrease/increase buttons. Keep this source library’s private support files with it; use only the public NumericInput import.',
};
const Fence = Text => '\n```luau\n'+Text.trimEnd()+'\n```\n';
function Contract(Name, Guides) {
  const Guide=Guides[Name==='Json'||Name==='JsonClient'?Name:Name==='NumericInput'?'LuiUI':'LuiWorkbench'];
  let Text=Guide;
  if (Name==='Commands') Text=Guide.slice(0,Guide.indexOf('## Documents'));
  if (Name==='Documents') Text=Guide.split('## Documents')[1].split('## Workspace and Split')[0];
  if (Name==='Documents') Text=Text.split('Stage a standalone command example with:')[0];
  if (Name==='Workspace'||Name==='Split') Text=Guide.split('## Workspace and Split')[1].split('## Preferences')[0];
  if (Name==='Preferences') Text=Guide.split('## Preferences')[1].split('## Integrated example')[0];
  if (Name==='Commands'||Name==='NumericInput') Text=Text.slice(Text.indexOf('```',Text.indexOf('```')+3)+3);
  Text=Text.replace(/```[\s\S]*?```/g,'').replace(/^# [^\n]*\n/gm,'').replace(/For next SDK/g,'For SDK').replace(/Next SDK preview\.2/g,'SDK preview.2');
  if (Name==='JsonClient') Text=Text.replace(/^An optional[\s\S]*?\n\n/,'').replace(/Version is `0\.1\.0`\.[\s\S]*$/,'The library version is `0.1.0`.');
  if (Name==='NumericInput') Text=Text.split('Scope and State are private support sources')[0]+'Scope and State are private cleanup/delivery support files. Keep them with NumericInput, but use NumericInput as the public component API.';
  Text=Text.replace(/The owner must grant Storage and\s+install a dedicated scope\./,'The app needs the Storage capability and an app-private scope.');
  return Text.trim();
}

export async function ImproveLibraries(Repository, Site, Version) {
  const Base='/LUI/docs/sdk-'+Version;const Root=Path.join(Site,'src/content/docs/docs/sdk-'+Version);
  const Guides={};for(const Name of ['LuiWorkbench','Json','JsonClient','LuiUI']) Guides[Name]=await ReadFile(Path.join(Repository,'libraries',Name,'README.md'),'utf8');
  const Reports=[];
  for(const Library of ['LuiWorkbench','Json','JsonClient','LuiUI']) {
    const Catalog=JSON.parse(await ReadFile(Path.join(Repository,'libraries',Library,'Library.json'),'utf8'));
    const Directory=Path.join(Root,'libraries',Library.toLowerCase());await MakeDirectory(Directory,{recursive:true});
    let Index='---\ntitle: '+Library+'\ndescription: '+JSON.stringify(Library==='LuiWorkbench'?'Commands, documents, two-pane layout and ordinary preferences.':Intros[Catalog.Modules[0].Name])+'\n---\n\n**Optional source libraries included in SDK '+Version+'.** These modules are not built-in services and are not loaded into your app until you declare/import them.\n\nStart with [using the bundled libraries]('+Base+'/guides/libraries/) for complete projects and the manifest/lock files.\n\n';
    for(const Module of Catalog.Modules) {
      const Name=Module.Name;const Sample=LibraryExamples[Name];if(!Sample)throw new Error('[LUI:Docs] Missing library example: '+Name);
      const Source=await ReadFile(Path.join(Repository,'libraries',Library,Name+'.luau'),'utf8');const Types=ModuleDeclarations(Source);
      if(Types.size!==Module.Types.length||Module.Types.some(Name=>!Types.has(Name)))throw new Error('[LUI:Docs] Library types differ');
      const Functions=(Module.Functions||['New']).map(Name=>Source.split(/\r?\n/).find(Line=>Line.startsWith('function '+Module.Name+'.'+Name+'('))?.replace(/^function /,''));
      if(Functions.some(Value=>!Value))throw new Error('[LUI:Docs] Library function differs');
      let Text='---\ntitle: '+Name+'\ndescription: '+JSON.stringify(Intros[Name])+'\n---\n\n**Optional app library · SDK '+Version+'.**\n\n'+Intros[Name]+'\n\n## Setup\n\n'+(Sample.Starter==='workbench'?'From the extracted SDK folder, create a sibling app with `./lui.cmd new ../MyWorkbench --template workbench`.':'Copy the SDK’s complete `Examples/'+Sample.Starter+'` folder to a new application folder outside the SDK.')+' Keep its Vendor files, lui.json, module lock and SDK pin together. See [library setup]('+Base+'/guides/libraries/).\n\nImport `require("@'+Library+'/'+Name+'")` after the project declares that library. An import copied into a basic starter is not enough.\n';
      if(Name==='Preferences')Text+='\nThis example needs `Storage` and `"Storage": {"ApplicationId": "com.example.notes"}` in lui.json. The workbench starter already supplies app-private storage. Use the same project folder when testing persistence.\n';
      if(Name==='JsonClient')Text+='\nRequires `network.client` and a running HTTP endpoint that returns valid JSON. The bundled json-client app supplies a complete local server/client example; the fragment below expects port8080.\n';
      if(Name==='Json')Text+='\nThe codec itself needs no capabilities. If you adapt the json-client project to use only Json, its network grants are not needed by this code.\n';
      Text+='\n## Example\n\nReplace `Src/Main.luau` in that prepared project with this complete file.\n'+Fence(Sample.Code)+'\n**Expected result:** '+Sample.Outcome+'\n\n## Functions\n'+Functions.map(Fence).join('')+'\n## Behavior and limits\n\n'+Contract(Name,Guides)+'\n';
      Text+='\n<details>\n<summary>Exported type declarations — advanced reference</summary>\n\nThese types describe returned models/controllers and their options. They are not additional classes to create.\n';
      for(const [Name,Declaration] of Types)Text+='\n### '+Name+'\n'+Fence(Declaration);
      Text+='\n</details>\n\n## Deeper implementation reading\n\nThe [library source](https://github.com/gmoddev/LUI/blob/7287bee1a46f3638ef7a9cb06c4830c753a10c75/libraries/'+Library+'/'+Name+'.luau) is useful when extending the framework or understanding cleanup. Private support modules such as Scope/State are implementation details, not additional supported app APIs.\n';
      await WriteFile(Path.join(Directory,Name.toLowerCase()+'.md'),Text);
      Index+=(Name==='Workspace'?'\n<span id="workspace-and-split"></span>\n':'')+'\n## '+Name+'\n\n'+Intros[Name]+'\n\n[Example, setup and full contract]('+Base+'/libraries/'+Library.toLowerCase()+'/'+Name.toLowerCase()+'/).\n';
      Reports.push({Library,Name,Starter:Sample.Starter,Code:Sample.Code,Types:[...Types.keys()]});
    }
    if(Library==='LuiWorkbench')Index+='\n## Integrated example\n\nFrom the SDK folder, use `./lui.cmd new ../MyWorkbench --template workbench` for the integrated app. [Library setup]('+Base+'/guides/libraries/) explains the included source and project files.\n\n## Native workload qualification\n\nSee [the tested profiles and remaining limits]('+Base+'/guides/platforms/). Source/module checks do not establish physical input or assistive-technology usability.\n';
    Index+='\n## What is not included\n\nThese modules use the existing runtime. They do not add arbitrary docking, unrestricted disk access, a package downloader or execution of text entered into CodeEditor. See [platform limits]('+Base+'/guides/platforms/). The library catalog’s Development label describes its experimental API maturity; these listed modules do ship in preview.2.\n';
    await WriteFile(Path.join(Directory,'index.md'),Index);
  }
  const Output=Path.join(Repository,'build/DocsLibraries',Version);await MakeDirectory(Output,{recursive:true});
  await WriteFile(Path.join(Output,'Examples.json'),JSON.stringify(Reports,null,2)+'\n');return {Modules:Reports.length,Examples:Reports.length};
}
