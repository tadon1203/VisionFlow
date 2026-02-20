#include <memory>

#include "VisionFlow/core/app.hpp"
#include "VisionFlow/input/makcu_controller.hpp"

int main() {
    auto mouseController = std::make_unique<vf::MakcuController>();
    vf::App app(std::move(mouseController));
    if (!app.run()) {
        return -1;
    }
    return 0;
}
