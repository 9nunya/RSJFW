#pragma once

#include "rsjfw/page.hpp"
#include "imgui.h"

namespace rsjfw {

class GUI;

class SettingsPage : public Page {
public:
    SettingsPage(GUI* gui);
    
    void render() override;
    std::string title() const override { return "Settings"; }
    
    void renderGeneralTab();
    void renderWineTab();
    void renderGpuTab();
    void renderFFlagsTab();
    void renderEnvTab();

private:
    GUI* gui_;
};

}
