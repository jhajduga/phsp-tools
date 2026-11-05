#pragma once
#include <TError.h>
namespace phsp {
// Keep ROOT warnings/errors; the application logger reports exported plots.
struct RootMessages {
    int previous = gErrorIgnoreLevel;
    RootMessages() { gErrorIgnoreLevel = kWarning; }
    ~RootMessages() { gErrorIgnoreLevel = previous; }
};
} // namespace phsp
