#include <memory>
#include <utility>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/input/mouse_controller_factory.hpp"

int main() {
    auto mouseController = vf::createMouseController();
    vf::App app(std::move(mouseController));
    if (!app.run()) {
        return -1;
    }
    return 0;
}
