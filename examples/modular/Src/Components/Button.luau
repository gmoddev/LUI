return function(Parent:Window|Frame,Name:string,Text:string,Callback:()->()):TextButton
    local Props:TextButtonInit={Parent=Parent,Name=Name,Text=Text,Size=UDim2.fromOffset(190,44)}
    local Button=Instance.new("TextButton",Props)
    Button.Activated:Connect(Callback)
    return Button
end
