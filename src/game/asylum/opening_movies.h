#pragma once
#include <string_view>
namespace bvr::aa::startup {
inline bool opening_movie_name(std::wstring_view path) {
    const auto slash=path.find_last_of(L"/\\");
    if(slash!=std::wstring_view::npos)path.remove_prefix(slash+1);
    auto equal=[](std::wstring_view a,std::wstring_view b){
        if(a.size()!=b.size())return false;
        for(size_t n=0;n<a.size();++n){auto c=a[n];if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';if(c!=b[n])return false;}
        return true;
    };
    if(path.size()>=4&&equal(path.substr(path.size()-4),L".bik"))path.remove_suffix(4);
    return equal(path,L"baa_logo_run_v5_h264")||equal(path,L"utlogo")||equal(path,L"legal")||equal(path,L"legalus")||equal(path,L"black");
}
}
