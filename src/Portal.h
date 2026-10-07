#pragma once

// HTTP configuration portal (port 80) plus the captive-portal DNS that makes
// phones pop the page open when they join the setup access point.
namespace portal {

void begin();
void loop();

}  // namespace portal
