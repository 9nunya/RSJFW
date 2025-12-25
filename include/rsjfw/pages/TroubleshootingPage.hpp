#pragma once

#include "rsjfw/page.hpp"
#include "imgui.h"

namespace rsjfw {

class TroubleshootingPage : public Page {
public:
    void render() override;
    std::string title() const override { return "Troubleshooting"; }
};

}
