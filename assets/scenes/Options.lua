local draft, refreshers
local CLICK_SFX = "assets/audio/click.ogg"
local entered, assetsPreloaded = false, false

-- Widget locals (assigned in OnEnter)
local backdrop, background, root, title, status
local pages, tabs = {}, {}

local function preloadAssets()
    if assetsPreloaded then return end
    Audio.Preload(CLICK_SFX, "Sfx")
    assetsPreloaded = true
end

local function text(parent, name, value)
    local label = parent:CreateFontString(name)
    label:SetText(value)
    label:SetColor(0xEBEDEAFF)
    label:SetMouseEnabled(false)
    label:SetFont("Inter-Regular", 20)
    return label
end

local function button(parent, name, value, action)
    local control = UI.CreateFrame("Button", name, parent)
    control:SetSize(136, 40)
    control:SetButtonColors(0x353B3FFF, 0x4C585FFF, 0x237F70FF)
    local label = text(control, name .. "Label", value)
    label:SetPoint("LEFT", control, "LEFT", 16, 0)
    control:SetScript("OnClick", function()
        Audio.Play(CLICK_SFX, "Sfx")
        action()
    end)
    return control, label
end

local function changed()
    status:SetText("Unsaved changes")
end

local function row(pageName, name, caption, index)
    local content = pages[pageName]:GetContent()
    local label = text(content, name .. "Caption", caption)
    label:SetPoint("TOPLEFT", content, "TOPLEFT", 0, index * 88)
    label:SetFont("Inter-Regular", 20)
    return content, index * 88 + 28
end

local function choice(pageName, name, caption, index, options, selected, select)
    local content, offset = row(pageName, name, caption, index)
    local control = UI.CreateFrame("DropDownBox", name, content)
    control:SetSize(136, 36)
    control:SetPoint("TOPLEFT", content, "TOPLEFT", 0, offset)
    control:SetPoint("TOPRIGHT", content, "TOPRIGHT", -20, offset)
    control:SetFont("Inter-Regular", 24)
    local labels = {}
    for optionIndex, option in ipairs(options) do labels[optionIndex] = option.label end
    control:SetOptions(labels)
    local refreshing = false
    local function refresh()
        refreshing = true
        for optionIndex, option in ipairs(options) do
            if option.label == selected() then
                control:SetSelectedIndex(optionIndex)
                break
            end
        end
        refreshing = false
    end
    table.insert(refreshers, refresh)
    refresh()
    control:SetScript("OnClick", function() Audio.Play(CLICK_SFX, "Sfx") end)
    control:SetScript("OnValueChanged", function()
        if refreshing then return end
        select(options[control:GetSelectedIndex()])
        Audio.Play(CLICK_SFX, "Sfx")
        changed()
    end)
end

local function checkbox(pageName, name, caption, index, field)
    local content, offset = row(pageName, name, caption, index)
    local control = UI.CreateFrame("CheckBox", name, content)
    control:SetSize(40, 32)
    control:SetPoint("TOPLEFT", content, "TOPLEFT", 0, offset)
    local function refresh() control:SetChecked(draft[field]) end
    table.insert(refreshers, refresh)
    refresh()
    control:SetScript("OnValueChanged", function()
        draft[field] = control:IsChecked()
        changed()
    end)
end

local function setLayout(width, height)
    if not root or width <= 0 or height <= 0 then return end
    local panelWidth = math.min(760, math.max(0, width - 48))
    local panelHeight = math.min(640, math.max(0, height - 48))
    root:SetSize(panelWidth, panelHeight)
    pages.Display:SetContentSize(math.max(0, panelWidth - 64), 264)
    pages.Audio:SetContentSize(math.max(0, panelWidth - 64), 440)
end

function OnEnter(width, height, initialState)
    preloadAssets()
    entered = true
    draft = Settings.Get()
    refreshers = {}
    pages = {}
    tabs = {}

    backdrop = UI.CreateFrame("Frame", "OptionsBackdrop")
    backdrop:SetAllPoints(UI.Root)
    backdrop:SetBackgroundColor(0x000000FF)

    background = backdrop:CreateImage("OptionsBackground")
    background:SetAllPoints(backdrop)
    background:SetMouseEnabled(false)
    background:SetFit("CONTAIN")
    background:SetSource("assets/images/splash_screen.png")

    root = UI.CreateFrame("Frame", "OptionsRoot", backdrop)
    root:SetSize(760, 640)
    root:SetPoint("CENTER", backdrop, "CENTER", 0, 0)
    root:SetBackgroundColor(0x141719D9)

    title = text(root, "OptionsTitle", "Options")
    title:SetFont("Inter-Bold", 32)
    title:SetPoint("TOPLEFT", root, "TOPLEFT", 32, 28)

    status = text(root, "OptionsStatus", "")
    status:SetPoint("BOTTOMLEFT", root, "BOTTOMLEFT", 32, -80)
    status:SetPoint("BOTTOMRIGHT", root, "BOTTOMRIGHT", -32, -80)
    status:SetWordWrap(true)

    for index, name in ipairs({"Display", "Audio"}) do
        local page = UI.CreateFrame("ScrollContainer", name .. "Page", root)
        page:SetPoint("TOPLEFT", root, "TOPLEFT", 32, 152)
        page:SetPoint("BOTTOMRIGHT", root, "BOTTOMRIGHT", -32, -116)
        page:SetContentSize(576, name == "Audio" and 440 or 264)
        page:SetVisible(index == 1)
        pages[name] = page
        tabs[name] = button(root, name .. "Tab", name, function()
            for other, otherPage in pairs(pages) do
                otherPage:SetVisible(other == name)
                tabs[other]:SetButtonColors(other == name and 0x237F70FF or 0x353B3FFF, 0x4C585FFF, 0x237F70FF)
            end
        end)
        tabs[name]:SetPoint("TOPLEFT", root, "TOPLEFT", 32 + (index - 1) * 148, 88)
    end
    tabs.Display:SetButtonColors(0x237F70FF, 0x4C585FFF, 0x237F70FF)

    local windowModes = {
        {label = "Windowed", fullscreen = false, exclusive = false},
        {label = "Borderless fullscreen", fullscreen = true, exclusive = false},
        {label = "Exclusive fullscreen", fullscreen = true, exclusive = true}
    }
    choice("Display", "WindowMode", "Window mode", 0, windowModes, function()
        return windowModes[draft.fullscreen and (draft.exclusiveFullscreen and 3 or 2) or 1].label
    end, function(option)
        draft.fullscreen, draft.exclusiveFullscreen = option.fullscreen, option.exclusive
    end)

    local function modeLabel(mode)
        return string.format("%d x %d / %d Hz", mode.width, mode.height, mode.refreshRate)
    end
    local displayModes = Settings.DisplayModes()
    local function addMode(w, h, r)
        for _, mode in ipairs(displayModes) do
            if mode.width == w and mode.height == h and mode.refreshRate == r then return end
        end
        table.insert(displayModes, {width = w, height = h, refreshRate = r})
    end
    addMode(draft.displayWidth, draft.displayHeight, draft.refreshRate)
    local defaults = Settings.Defaults()
    addMode(defaults.displayWidth, defaults.displayHeight, defaults.refreshRate)
    for _, mode in ipairs(displayModes) do mode.label = modeLabel(mode) end
    choice("Display", "Resolution", "Resolution / refresh rate", 1, displayModes, function()
        return modeLabel({width = draft.displayWidth, height = draft.displayHeight, refreshRate = draft.refreshRate})
    end, function(option)
        draft.displayWidth, draft.displayHeight, draft.refreshRate = option.width, option.height, option.refreshRate
    end)

    checkbox("Display", "VSync", "Vertical sync", 2, "vSyncEnabled")

    for index, volume in ipairs({{"Master", "masterVolume"}, {"Music", "musicVolume"}, {"Effects", "sfxVolume"}}) do
        local vname, field = volume[1], volume[2]
        local content, offset = row("Audio", vname, vname .. " volume", index - 1)
        local slider = UI.CreateFrame("StatusBar", vname .. "Volume", content)
        slider:SetSize(240, 28)
        slider:SetPoint("TOPLEFT", content, "TOPLEFT", 0, offset)
        slider:SetPoint("TOPRIGHT", content, "TOPRIGHT", -84, offset)
        slider:SetMinMaxValues(0, 1)
        local value = text(content, vname .. "Value", "")
        value:SetPoint("TOPRIGHT", content, "TOPRIGHT", -20, offset + 4)
        local refreshing = false
        local function updateVolume()
            local success, message = Settings.SetVolume(field, draft[field])
            if not success then status:SetText(message or "Unable to save volume") end
        end
        local function refresh()
            refreshing = true
            slider:SetValue(draft[field])
            value:SetText(string.format("%d%%", math.floor(draft[field] * 100 + 0.5)))
            refreshing = false
        end
        table.insert(refreshers, function() refresh(); updateVolume() end)
        refresh()
        slider:SetScript("OnValueChanged", function()
            if refreshing then return end
            draft[field] = slider:GetValue()
            value:SetText(string.format("%d%%", math.floor(draft[field] * 100 + 0.5)))
            updateVolume()
        end)
    end

    local devices = {{label = "System default", value = ""}}
    local foundDevice = draft.audioDevice == ""
    for _, device in ipairs(Settings.AudioDevices()) do
        table.insert(devices, {label = device, value = device})
        if device == draft.audioDevice then foundDevice = true end
    end
    if not foundDevice then table.insert(devices, {label = draft.audioDevice, value = draft.audioDevice}) end
    choice("Audio", "AudioDevice", "Output device", 3, devices, function()
        return draft.audioDevice == "" and "System default" or draft.audioDevice
    end, function(option) draft.audioDevice = option.value end)
    checkbox("Audio", "MuteUnfocused", "Mute when unfocused", 4, "muteWhenUnfocused")

    local back = button(root, "BackButton", "Back", function() Scene.GoTo("MainMenu") end)
    back:SetPoint("BOTTOMLEFT", root, "BOTTOMLEFT", 32, -24)
    local reset = button(root, "DefaultsButton", "Defaults", function()
        draft = Settings.Defaults()
        for _, refresh in ipairs(refreshers) do refresh() end
        changed()
    end)
    reset:SetPoint("BOTTOMRIGHT", root, "BOTTOMRIGHT", -180, -24)
    local apply = button(root, "ApplyButton", "Apply", function()
        local success, message = Settings.Apply(draft)
        status:SetText(message or (success and "Settings applied" or "Unable to apply settings"))
    end)
    apply:SetPoint("BOTTOMRIGHT", root, "BOTTOMRIGHT", -32, -24)
    apply:SetButtonColors(0x237F70FF, 0x2B9A84FF, 0x196555FF)

    setLayout(width, height)
end

function OnUpdate(state, dt)
end

function OnLayoutChanged(width, height)
    if not entered then return end
    setLayout(width, height)
end

function OnExit()
    entered = false
end

function OnShortcut(scanCode)
    return false
end
