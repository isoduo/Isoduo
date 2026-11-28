#pragma once
#include "isoduo/semantics.hpp"

namespace test {
inline bool drawingFramesPaused(isoduo::Ui& ui) {
    const auto animating = ui.animating();
    const auto semantic = ui.semantics().animating();
    return animating && semantic && !*animating && !*semantic;
}
}
