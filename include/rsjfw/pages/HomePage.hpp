#ifndef RSJFW_HOMEPAGE_HPP
#define RSJFW_HOMEPAGE_HPP

#include "rsjfw/page.hpp"
#include "imgui.h"
#include <GL/gl.h>

namespace rsjfw {

class GUI;

class HomePage : public Page {
public:
    HomePage(GUI* gui, GLuint logoTexture, int logoWidth, int logoHeight);
    
    void render() override;
    std::string title() const override { return "Home"; }
    bool canGoBack() const override { return false; }

private:
    GUI* gui_;
    GLuint logoTexture_;
    int logoWidth_;
    int logoHeight_;
};

} // namespace rsjfw

#endif // RSJFW_HOMEPAGE_HPP
