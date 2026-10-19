#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_ros2/launch_incarnation.hpp"
#include "gtest/gtest.h"

namespace
{

TEST(LaunchIncarnation, ProgrammaticOverrideBeatsQualifiedFilesAndCli)
{
  const auto path =
      std::filesystem::temp_directory_path() /
      ("ug-pin-priority-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
  struct RemoveFile
  {
    std::filesystem::path path;
    ~RemoveFile()
    {
      std::filesystem::remove(path);
    }
  } cleanup{path};

  for (const std::string ns : {"", "/robot", "/fleet/robot"})
  {
    for (const auto& pair : {
             std::make_pair("universal_gnss_receiver", "source_incarnation"),
             std::make_pair("universal_gnss_ntrip", "expected_source_incarnation"),
         })
    {
      const std::string name = pair.first;
      const std::string parameter = pair.second;
      {
        std::ofstream file(path);
        file << ns << '/' << name << ":\n  ros__parameters:\n    " << parameter
             << ": stale-file\n    other_parameter: 7\n";
      }
      const std::string cli = name + ':' + parameter + ":=stale-cli";
      const std::string path_text = path.string();
      const std::vector<const char*> argv{
          "priority-probe", "--ros-args", "--params-file", path_text.c_str(), "-p", cli.c_str()};
      auto context = std::make_shared<rclcpp::Context>();
      context->init(static_cast<int>(argv.size()), argv.data());
      {
        rclcpp::NodeOptions options;
        options.context(context);
        options.append_parameter_override(parameter, "0123456789abcdef0123456789abcdef");
        rclcpp::Node node(name, ns, options);
        EXPECT_EQ(node.declare_parameter<std::string>(parameter, "default"),
                  "0123456789abcdef0123456789abcdef");
        EXPECT_EQ(node.declare_parameter<int>("other_parameter", 0), 7);
      }
      context->shutdown("priority probe complete");
    }
  }
}

TEST(LaunchIncarnation, FilteringPreservesOriginalBytesAndAllOtherArguments)
{
  const std::vector<std::string> original{
      "receiver_node",
      "--ug-launch-incarnation=0123456789abcdef0123456789abcdef",
      "--ros-args",
      "--params-file",
      "configuration with spaces.yaml",
      "-r",
      "__ns:=/robot",
      "-p",
      "other:=--ug-launch-incarnation=ordinary-value"};
  std::vector<std::string> input = original;
  std::vector<const char*> argv;
  for (const auto& argument : input)
  {
    argv.push_back(argument.c_str());
  }
  const auto parsed = universal_gnss_ros2::ParseLaunchArguments(argv.size(), argv.data());
  EXPECT_EQ(input, original);
  EXPECT_EQ(parsed.incarnation, "0123456789abcdef0123456789abcdef");
  auto expected = original;
  expected.erase(expected.begin() + 1);
  EXPECT_EQ(parsed.ros_arguments, expected);
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    EXPECT_EQ(argv[i], input[i].c_str());
  }
}

TEST(LaunchIncarnation, AbsentFlagKeepsLegacyArguments)
{
  const char* argv[]{"receiver_node", "--ros-args", "-p", "source_incarnation:=legacy-value"};
  const auto parsed = universal_gnss_ros2::ParseLaunchArguments(4, argv);
  EXPECT_FALSE(parsed.incarnation);
  EXPECT_EQ(parsed.ros_arguments, std::vector<std::string>(argv, argv + 4));
}

TEST(LaunchIncarnation, InvalidAndDuplicateArgumentsAreRejected)
{
  for (const std::string value : {
           "--ug-launch-incarnation",
           "--ug-launch-incarnation=",
           "--ug-launch-incarnation=not-a-uuid",
           "--ug-launch-incarnation=0123456789ABCDEF0123456789ABCDEF",
           "--ug-launch-incarnation=0123456789abcdef0123456789abcdeg",
           "--ug-launch-incarnation=0123456789abcdef0123456789abcdef0",
       })
  {
    const char* argv[]{"receiver_node", value.c_str()};
    EXPECT_THROW(universal_gnss_ros2::ParseLaunchArguments(2, argv), std::invalid_argument);
  }
  for (const char* second : {"--ug-launch-incarnation=0123456789abcdef0123456789abcdef",
                             "--ug-launch-incarnation=ffffffffffffffffffffffffffffffff"})
  {
    const char* argv[]{
        "receiver_node", "--ug-launch-incarnation=0123456789abcdef0123456789abcdef", second};
    EXPECT_THROW(universal_gnss_ros2::ParseLaunchArguments(3, argv), std::invalid_argument);
  }
  EXPECT_THROW(universal_gnss_ros2::ParseLaunchArguments(0, nullptr), std::invalid_argument);
}

TEST(LaunchIncarnation, GuardRejectsDivergenceWithoutBlockingOtherParameters)
{
  auto context = std::make_shared<rclcpp::Context>();
  const char* argv[]{"parameter-guard"};
  context->init(1, argv);
  {
    rclcpp::NodeOptions options;
    options.context(context);
    rclcpp::Node node("parameter_guard", options);
    node.declare_parameter<std::string>("pin", "0123456789abcdef0123456789abcdef");
    node.declare_parameter<int>("other", 1);
    auto guard = universal_gnss_ros2::GuardLaunchIncarnation(
        node, "pin", "0123456789abcdef0123456789abcdef");
    EXPECT_FALSE(node.set_parameter(rclcpp::Parameter("pin", "different")).successful);
    EXPECT_FALSE(node.set_parameter(rclcpp::Parameter("pin")).successful);
    EXPECT_FALSE(node.set_parameter(rclcpp::Parameter("pin", 1)).successful);
    EXPECT_TRUE(node.set_parameter(rclcpp::Parameter("pin", "0123456789abcdef0123456789abcdef"))
                    .successful);
    EXPECT_EQ(node.get_parameter("pin").as_string(), "0123456789abcdef0123456789abcdef");
    EXPECT_TRUE(node.set_parameter(rclcpp::Parameter("other", 2)).successful);
    EXPECT_EQ(node.get_parameter("other").as_int(), 2);
  }
  context->shutdown("guard test complete");
}

}  // namespace
