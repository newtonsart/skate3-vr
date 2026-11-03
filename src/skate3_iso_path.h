#pragma once
#include <string_view>
#include <string>

namespace skate3 {
inline bool IsUnsafeIsoPath(std::string_view path) {
  if (path.empty() || path.front()=='/') return true;
  for (unsigned char c : path)
    if (c<32 || std::string_view("\\:<>\"|?*").find(char(c))!=std::string_view::npos) return true;
  size_t start=0;
  while (start<=path.size()) {
    const auto end=path.find('/',start);
    const auto component=path.substr(start,end==std::string_view::npos ? path.size()-start : end-start);
    if (component.empty() || component=="." || component==".." || component.back()=='.' || component.back()==' ')
      return true;
    std::string device(component.substr(0,component.find('.')));
    for (auto& c : device) if (c>='a' && c<='z') c=char(c-'a'+'A');
    if (device=="CON" || device=="PRN" || device=="AUX" || device=="NUL" ||
        (device.size()==4 && (device.starts_with("COM") || device.starts_with("LPT")) && device[3]>='1' && device[3]<='9'))
      return true;
    if (end==std::string_view::npos) break;
    start=end+1;
  }
  return false;
}
}
