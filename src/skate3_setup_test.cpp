#include "skate3_iso_path.h"
#include <stdexcept>

int main() {
  for (auto path : {"default.xex","data/webkit/EAWebkit.xex","data/a name.bin"})
    if (skate3::IsUnsafeIsoPath(path)) throw std::runtime_error("Valid ISO path rejected");
  for (auto path : {"","/absolute","../escape","a/../../escape","C:/escape","a\\..\\escape",
      "file:stream","a//b","a/","a/./b","trailing.","trailing ","a\nfile","a?file","NUL.txt","a/COM1","a/con"})
    if (!skate3::IsUnsafeIsoPath(path)) throw std::runtime_error("Unsafe ISO path accepted");
}
