return function()
    local Window=Instance.new('Window',{Title='LUI launch qualification',Size=UDim2.fromOffset(320,180)})
    Instance.new('TextLabel',{Name='Content',Text='Launcher smoke',Parent=Window})
    Window.Visible=true
    return Window
end
