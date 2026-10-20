// Update checker implementation for builds without libcurl (Ymir_ENABLE_UPDATE_CHECKER=OFF), such as UWP.
// Every check fails with an explanatory message, so the UI reports updates as unavailable.
#include "update_checker.hpp"

namespace app {

UpdateChecker::UpdateChecker() = default;

UpdateChecker::~UpdateChecker() = default;

UpdateResult UpdateChecker::Check(ReleaseChannel channel, std::filesystem::path cacheRoot, UpdateCheckMode mode) {
    return UpdateResult::Failed("Update checks are not available in this build");
}

} // namespace app
