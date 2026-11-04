#include "skate3_safe_file.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

int main() {
  namespace fs = std::filesystem;
  const auto dir=fs::temp_directory_path() / ("skate3-safe-file-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directory(dir);
  try {
    const auto path=dir / "update.xexp";
    std::string error;
    const std::string bytes("new\0data",8);
    if (!skate3::safe_file::WriteFileAtomic(path,"old",&error) ||
        !skate3::safe_file::WriteFileAtomic(path,bytes,&error) ||
        skate3::safe_file::ReadWholeFile(path).bytes!=bytes)
      throw std::runtime_error("binary atomic replacement failed: "+error);
    fs::create_directory(path.string()+".tmp");
    if (skate3::safe_file::WriteFileAtomic(path,"changed",&error) ||
        skate3::safe_file::ReadWholeFile(path).bytes!=bytes)
      throw std::runtime_error("failed replacement changed original");
  } catch (...) { fs::remove_all(dir); throw; }
  fs::remove_all(dir);
}
