#pragma once
#include <array>
#include <string_view>
namespace xrfg {
inline constexpr bool origin_title_equal(std::wstring_view a,std::wstring_view b) {
    if(a.size()!=b.size())return false;
    const auto lower=[](wchar_t c){return c>=L'A'&&c<=L'Z'?wchar_t(c+L'a'-L'A'):c;};
    for(std::size_t i=0;i<a.size();++i)if(lower(a[i])!=lower(b[i]))return false;
    return true;
}
inline constexpr std::array<unsigned,2> luke_origin_inset_for_title(std::wstring_view name) {
    if(origin_title_equal(name,L"Outlaws.exe"))return {256,32};
    if(origin_title_equal(name,L"GTA5_Enhanced.exe"))return {0,32};
    return {0,0};
}
}
