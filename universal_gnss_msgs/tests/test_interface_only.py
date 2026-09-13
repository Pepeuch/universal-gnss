from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]


def test_package_contains_only_ros_interfaces() -> None:
    assert {path.name for path in (PACKAGE_ROOT / "msg").glob("*.msg")} == {
        "GnssStatus.msg",
        "RtcmFrame.msg",
    }
    assert {path.name for path in (PACKAGE_ROOT / "srv").glob("*.srv")} == {
        "GetReceiverSnapshot.srv",
    }
    assert not (PACKAGE_ROOT / "src").exists()
    assert not any(PACKAGE_ROOT.rglob("*.cpp"))
    assert not any(PACKAGE_ROOT.rglob("*.hpp"))


def test_cmake_defines_no_runtime_targets() -> None:
    cmake = (PACKAGE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "rosidl_generate_interfaces(" in cmake
    for forbidden in ("add_executable(", "add_library(", "add_subdirectory("):
        assert forbidden not in cmake
