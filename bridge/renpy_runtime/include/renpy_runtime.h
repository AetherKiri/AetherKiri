#pragma once

namespace aetherkiri::renpy {

// Registers the opt-in Ren'Py SDK subprocess provider. The provider only
// builds when AETHERKIRI_RENPY_SDK_ROOT names an official SDK checkout. The
// desktop host imports frames through the validated RGBA compatibility path;
// direct native GPU texture sharing remains unsupported.
void RegisterRuntimeProvider();

}  // namespace aetherkiri::renpy
