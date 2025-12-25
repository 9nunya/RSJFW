#pragma once

#include "rsjfw/wine.hpp"
#include <string>

namespace rsjfw {
namespace dxvk {

// Installs DXVK DLLs from the extracted directory into the Wine Prefix
bool install(wine::Prefix& pfx, const std::string& dxvkRootDir);

// Configures WINEDLLOVERRIDES for DXVK
void envOverride(wine::Prefix& pfx, bool enabled);

} // namespace dxvk
} // namespace rsjfw
