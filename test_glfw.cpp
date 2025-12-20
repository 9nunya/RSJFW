#include <GLFW/glfw3.h>
#include <iostream>

int main() {
    if (!glfwInit()) {
        const char* description;
        glfwGetError(&description);
        std::cerr << "GLFW Init Failed: " << (description ? description : "Unknown") << std::endl;
        return 1;
    }
    std::cout << "GLFW Init Success!" << std::endl;
    
    // Create hidden window
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 480, "Test", NULL, NULL);
    if (!window) {
        const char* description;
        glfwGetError(&description);
        std::cerr << "Window Creation Failed: " << (description ? description : "Unknown") << std::endl;
        glfwTerminate();
        return 1;
    }
    
    std::cout << "Window Created!" << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
