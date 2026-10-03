return function()
    local layoutBindings = setmetatable({}, {__mode = "k"})
    local textBindings = setmetatable({}, {__mode = "k"})
    local visibleBindings = setmetatable({}, {__mode = "k"})
    local colorBindings = setmetatable({}, {__mode = "k"})
    local mouseBindings = setmetatable({}, {__mode = "k"})
    local buttonColorBindings = setmetatable({}, {__mode = "k"})
    local valueBindings = setmetatable({}, {__mode = "k"})
    local tooltipBindings = setmetatable({}, {__mode = "k"})
    local contentSizeBindings = setmetatable({}, {__mode = "k"})
    local stateBindings = {}
    local stateValues = {}

    local function numberEqual(left, right)
        if left == right then return true end
        if type(left) ~= "number" or type(right) ~= "number" then return false end
        return math.abs(left - right) < 0.01
    end

    local binder = {}

    function binder.place(widget, parent, left, top, width, height)
        width = math.max(0, width)
        height = math.max(0, height)
        local binding = layoutBindings[widget]
        if binding and binding.parent == parent and numberEqual(binding.left, left) and numberEqual(binding.top, top)
            and numberEqual(binding.width, width) and numberEqual(binding.height, height) then
            return
        end
        widget:ClearPoints()
        widget:SetSize(width, height)
        widget:SetPoint("TOPLEFT", parent, "TOPLEFT", left, top)
        layoutBindings[widget] = {parent = parent, left = left, top = top, width = width, height = height}
    end

    function binder.text(widget, value)
        if textBindings[widget] == value then return end
        widget:SetText(value)
        textBindings[widget] = value
    end

    function binder.visible(widget, visible)
        if visibleBindings[widget] == visible then return end
        widget:SetVisible(visible)
        visibleBindings[widget] = visible
    end

    function binder.color(widget, color)
        if colorBindings[widget] == color then return end
        widget:SetColor(color)
        colorBindings[widget] = color
    end

    function binder.mouseEnabled(widget, enabled)
        if mouseBindings[widget] == enabled then return end
        widget:SetMouseEnabled(enabled)
        mouseBindings[widget] = enabled
    end

    function binder.buttonColors(widget, normal, hover, selected)
        local binding = buttonColorBindings[widget]
        if binding and binding.normal == normal and binding.hover == hover and binding.selected == selected then return end
        widget:SetButtonColors(normal, hover, selected)
        buttonColorBindings[widget] = {normal = normal, hover = hover, selected = selected}
    end

    function binder.value(widget, value)
        if numberEqual(valueBindings[widget], value) then return end
        widget:SetValue(value)
        valueBindings[widget] = value
    end

    function binder.tooltip(widget, value)
        if tooltipBindings[widget] == value then return end
        widget:SetTooltip(value)
        tooltipBindings[widget] = value
    end

    function binder.contentSize(widget, width, height)
        local binding = contentSizeBindings[widget]
        if binding and numberEqual(binding.width, width) and numberEqual(binding.height, height) then return end
        widget:SetContentSize(width, height)
        contentSizeBindings[widget] = {width = width, height = height}
    end

    function binder.defineState(key, getter, apply)
        stateBindings[#stateBindings + 1] = {key = key, getter = getter, apply = apply}
    end

    function binder.applyState(context)
        for _, binding in ipairs(stateBindings) do
            local value = binding.getter(context)
            local cacheKey = binding.key
            if stateValues[cacheKey] ~= value then
                binding.apply(value, context)
                stateValues[cacheKey] = value
            end
        end
    end

    return binder
end
