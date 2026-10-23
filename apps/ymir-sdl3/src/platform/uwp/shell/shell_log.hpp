#pragma once

/**
@file
@brief Dev log group of the Xbox shell.
*/

#include <app/shared_context.hpp>

#include <ymir/util/dev_log.hpp>

#include <fmt/std.h>

namespace platform::uwp::shell::grp {

struct shell : public app::grp::base {
    static constexpr std::string_view name = "UWP-Shell";
};

} // namespace platform::uwp::shell::grp
