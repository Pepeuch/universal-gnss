#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"

namespace universal_gnss_ros2
{

struct LaunchArguments
{
  std::optional<std::string> incarnation;
  std::vector<std::string> ros_arguments;
};

// Own the filtered arguments: neither the original pointer array nor its bytes
// are modified, so the process argv still exposes the launch-owned pin.
inline LaunchArguments ParseLaunchArguments(int argc, const char* const* argv)
{
  if (argc < 1 || argv == nullptr || argv[0] == nullptr)
  {
    throw std::invalid_argument("missing executable argument");
  }
  const std::string option = "--ug-launch-incarnation";
  const std::string prefix = option + '=';
  LaunchArguments parsed;
  parsed.ros_arguments.emplace_back(argv[0]);
  for (int index = 1; index < argc; ++index)
  {
    if (argv[index] == nullptr)
    {
      throw std::invalid_argument("null process argument");
    }
    const std::string argument = argv[index];
    if (argument == option)
    {
      throw std::invalid_argument("--ug-launch-incarnation requires =<32 lowercase hex digits>");
    }
    if (argument.compare(0, prefix.size(), prefix) != 0)
    {
      parsed.ros_arguments.push_back(argument);
      continue;
    }
    if (parsed.incarnation)
    {
      throw std::invalid_argument("duplicate --ug-launch-incarnation");
    }
    const std::string token = argument.substr(prefix.size());
    if (token.size() != 32 || !std::all_of(token.begin(), token.end(), [](char character) {
          return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        }))
    {
      throw std::invalid_argument("--ug-launch-incarnation requires 32 lowercase hex digits");
    }
    parsed.incarnation = token;
  }
  return parsed;
}

inline rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr GuardLaunchIncarnation(
    rclcpp::Node& node, const std::string& parameter_name, const std::string& incarnation)
{
  return node.add_on_set_parameters_callback(
      [parameter_name, incarnation](const std::vector<rclcpp::Parameter>& parameters) {
        rcl_interfaces::msg::SetParametersResult result;
        result.successful = true;
        for (const auto& parameter : parameters)
        {
          if (parameter.get_name() == parameter_name &&
              (parameter.get_type() != rclcpp::ParameterType::PARAMETER_STRING ||
               parameter.as_string() != incarnation))
          {
            result.successful = false;
            result.reason = "parameter is pinned by --ug-launch-incarnation";
            break;
          }
        }
        return result;
      });
}

template <typename NodeType>
int RunWithLaunchIncarnation(int argc, char** argv, const std::string& parameter_name)
{
  LaunchArguments parsed;
  try
  {
    parsed = ParseLaunchArguments(argc, argv);
  }
  catch (const std::invalid_argument& error)
  {
    std::cerr << "UG launch argument error: " << error.what() << '\n';
    return 2;
  }
  std::vector<const char*> ros_argv;
  for (const auto& argument : parsed.ros_arguments)
  {
    ros_argv.push_back(argument.c_str());
  }
  rclcpp::init(static_cast<int>(ros_argv.size()), ros_argv.data());
  rclcpp::NodeOptions options;
  if (parsed.incarnation)
  {
    options.append_parameter_override(parameter_name, *parsed.incarnation);
  }
  auto node = std::make_shared<NodeType>(options);
  // No executor runs before this guard is installed. Legacy nodes keep their
  // existing parameter behavior when the native flag is absent.
  auto guard = parsed.incarnation
                   ? GuardLaunchIncarnation(*node, parameter_name, *parsed.incarnation)
                   : nullptr;
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}

}  // namespace universal_gnss_ros2
