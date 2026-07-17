#pragma once

#include "lua.hpp"

#include <SFML/Window.hpp>
#include <cstdint>
#include <imgui.h>
#include <volk.h>

class VulkanContext;

class ImGuiLayer {
  public:
    ImGuiLayer(lua_State* L);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void initializeVulkanBackend(const VulkanContext& context);

    void processEvent(const sf::Event& event);

    void setDeltaTime(float deltaSeconds);
    void setDisplaySize(uint32_t width, uint32_t height);

    void beginFrame();
    void endFrame();
    void renderDrawData(VkCommandBuffer commandBuffer);

    ImFont* headingFont() const {
        return headingFont_;
    }
    ImFont* titleFont() const {
        return titleFont_;
    }
  private:
    static ImGuiKey translateSfmlKeyToImGui(sf::Keyboard::Key key);

    void initLuaBindings();
    void applyModernStyle();
    void loadUiFonts();

    lua_State* L_ = nullptr;
    ImFont* headingFont_ = nullptr;
    ImFont* titleFont_ = nullptr;
};
